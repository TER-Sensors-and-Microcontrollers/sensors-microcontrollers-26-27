# serial-demo.py
# Reads incoming can packet from the on-car sender
# 
# 

from globals import *


current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)

app = Flask(__name__)
DATABASE = os.path.join(current_dir, 'database.db')




# helper func that returns 8 bytes, randomized (CAN DATA bytestring)
def randomize_bytes():
    return os.urandom(8)

# get flask app DB out-of-context (separate file)
def get_db():
    # required to work with flask app outside of the program
    with app.app_context():
        db = getattr(g, '_database', None)
        if db is None:
            db = g._database = sqlite3.connect(DATABASE)
            db.row_factory = sqlite3.Row # Optional: access rows as dictionary-like objects
        return db

# [TEST] given open serial port object, indefinitely writes mock CAN data 
# (randomized CAN data frames + fixed set of CAN IDs) to serial port
# does not return anything
# BIG ENDIAN!
def feeder(ser:serial.Serial):
    fids = [161, 162, 165, 166, 167, 172] # Changd to IDs the reader actually handles
    weights = [1, 1, 1, 1, 1, 1] # Made weight of each ID equal
    start = time.time()
    while True:
        bytes_ = randomize_bytes()
        fid = random.choices(fids, weights=weights)[0]
        
        fid_bytes = fid.to_bytes(1, 'big') if fid > 2048 else fid.to_bytes(2, 'big')
        # print("FID ", fid, "FID LENGTH ",len(fid_bytes))
        # print("DATA LENGTH ", len(bytes_))
        data_to_send = fid_bytes + len(bytes_).to_bytes(1,'big') + bytes_ + b"\n"
        ser.write(data_to_send)
        time.sleep(.005)

# given incoming CAN frame, parse and return appropriate ID,DATA,DLC values.
# if incoming frame is not of valid length, return all -1s
# UNPACKS CAN_ID AS BIG ENDIAN!
def parse_frame(frame:bytes):
    if len(frame) < 3 or len(frame) > 12:
        print("Error parsing current frame!")
        print(f"Frame: {frame}")
        return -1,-1,-1
    can_id = (((frame[0] << 8) | frame[1]) & 0x7FF)
    dlc = frame[2]
    data = frame[3:3+dlc]
    if can_id < 0 or can_id > 2048 or dlc < 0 or dlc > 8:
        print("Error: Malformed CAN frame")
        print(f"Frame: {frame}")
        return -1,-1,-1
    return can_id, dlc, data

# given open serial port object, indefinitely reads icoming CAN frame data 
# from serial port, and stores it in Flask database. does not return anything
# prints debug messages if debug flag set to True
def reader(ser:serial.Serial, debug=True):
    db = get_db()
    cursor = db.cursor()

    # --- Batching Setup ---
    # Readings are collected in `batch` and written to the database all at once,
    # either when the batch hits BATCH_SIZE rows or FLUSH_INTERVAL seconds have passed.
    BATCH_SIZE = 50
    FLUSH_INTERVAL = 0.5 # seconds
    batch = []
    last_flush = time.time()

    def flush():
        # Write everything in the batch with one executemany and one commit, then empty it.
        nonlocal last_flush
        if batch:
            cursor.executemany(
                "INSERT INTO sensor_readings (sensor_id, name, data, unit, timestamp) VALUES (?, ?, ?, ?, ?)",
                batch)
            db.commit()
            print(f"Flushed {len(batch)} readings")   # temporary, for testing
            batch.clear()
            last_flush = time.time()

    def save(reading):
        # Add one reading to the batch instead of writing it right away.
        batch.append(reading)
            
    start = time.time()
    try:
        while True:
            if ser.in_waiting > 0:
                frame = ser.readline()
                id,dlc,data = parse_frame(frame)
                if id == -1:
                    if debug:
                        print("Skipping frame")
                    continue
                
                if debug:
                    print(f"RECIEVED: {time.time() - start:.2f}    ID: {id}    DLC: {dlc}    DATA: {data}")
                if len(data) != 8: # skip chopped messages [tentatively]
                    print(f"Error: data length is not 8!")
                    continue
                    
                match id:
                    # interpret data as appropriate type per case
                        # START MC Values - Handle as Little Endian
                        case 160: # 'A0' - template value
                            pass
                        case 161: # Temp 2
                            cb_temp = np.float32(struct.unpack('<H', data[0:2])[0])
                            reading = [id * CONST_OFFSET, "CB Temp", float(cb_temp),"C", time.time()]
                            save(reading)
                        case 162: # Temp 3
                            cool_temp = np.float32(struct.unpack('<H', data[0:2])[0])
                            htspt_temp = np.float32(struct.unpack('<H', data[2:4])[0])
                            mot_temp = np.float32(struct.unpack('<H', data[4:6])[0])
                            readingCool = [id * CONST_OFFSET, "Coolant Temp", float(cool_temp),"C", time.time()]
                            readingHtspt = [id * CONST_OFFSET + 1, "Hotspot Temp", float(htspt_temp),"C", time.time()]
                            readingMot = [id * CONST_OFFSET + 2, "Motor Temp", float(mot_temp),"C", time.time()]
                            save(readingCool)
                            save(readingHtspt)
                            save(readingMot)
                        case 163: # Analog Input - Not impl. this year
                            pass
                        case 164: # Dig. Input Status
                            pass
                        case 165: # Motor Pos.
                            motor_angle = np.float32(struct.unpack('<H', data[0:2])[0])
                            motor_speed = np.float32(struct.unpack('<H', data[2:4])[0])
                            readingA = [id * CONST_OFFSET, "Motor Angle", float(motor_angle),"Degrees", time.time()]
                            readingS = [id * CONST_OFFSET + 1, "Motor Speed", float(motor_speed),"RPM", time.time()]
                            save(readingA)
                            save(readingS)
                        case 166: # Current Info
                            dc_curr = np.float32(struct.unpack('<H', data[6:])[0])
                            reading = [id * CONST_OFFSET, "DC Current", float(dc_curr),"Amps", time.time()]
                            save(reading)
                        case 167: # Voltage Info
                            dc_volt = np.float32(struct.unpack('<H', data[0:2])[0])
                            reading = [id * CONST_OFFSET, "DC Voltage", float(dc_volt),"Volts", time.time()]
                            save(reading)
                        case 170: # Internal States
                            vsm_state = int(data[0])
                            inv_state = int(data[2])
                            other = int(data[7])
                            readingV = [id * CONST_OFFSET, "VSM State", vsm_state,"Bits", time.time()]
                            readingI = [id * CONST_OFFSET + 1, "Inverter State", inv_state,"Bits", time.time()]
                            readingO = [id * CONST_OFFSET + 2, "DC Voltage", other,"Bits", time.time()]
                            save(readingV)
                            save(readingI)
                            save(readingO)
                        case 171: # Fault codes
                            post_fault_lo = int(struct.unpack('<H', data[0:2])[0])
                            post_fault_hi = int(struct.unpack('<H', data[2:4])[0])
                            run_fault_lo = int(struct.unpack('<H', data[4:6])[0])
                            run_fault_hi = int(struct.unpack('<H', data[6:])[0])
                            readingPl= [id * CONST_OFFSET, "Low Post Faults", post_fault_lo,"Bits", time.time()]
                            readingPh = [id * CONST_OFFSET + 1, "High Post Faults", post_fault_hi,"Bits", time.time()]
                            readingRl= [id * CONST_OFFSET + 2, "Low Post Faults", run_fault_lo,"Bits", time.time()]
                            readingRh = [id * CONST_OFFSET + 3, "High Run Faults", run_fault_hi,"Bits", time.time()]
                            save(readingPl)
                            save(readingPh)
                            save(readingRl)
                            save(readingRh)
                        case 172: # Torque / Timer
                            torque = np.float32(struct.unpack('<H', data[0:2])[0])
                            timer = np.float32(struct.unpack('<H', data[2:4])[0])
                            readingTq = [id * CONST_OFFSET, "Torque", float(torque),"N.m.", time.time()]
                            readingTm = [id * CONST_OFFSET + 1, "Inverter State", float(timer * .003),"Seconds", time.time()]
                            save(readingTq)
                            save(readingTm)
                        # END MC values
                        case _:
                            pass
            else:
                # Only pause when there's nothing to read, and only briefly.
                # The old time.sleep(0.5) ran after EVERY frame, capping the reader at ~2 frames/sec.
                time.sleep(0.005)
            
            if len(batch) >= BATCH_SIZE or time.time() - last_flush >= FLUSH_INTERVAL:
                flush() 

    finally:
        # Runs when the loop ends for any reason (Ctrl+C, an error),
        # so readings still in the batch get saved instead of lost.
        print("Final flush:")
        flush()    



if __name__ == "__main__":
    ser = None
    print(DATABASE)
    try:
        # loop:// creates a virtual serial port entirely in memory
        # ser = serial.Serial(PI_RADIO, baudrate=115200, timeout=1)
        # print(f"Connected successfully to {PI_RADIO}")
        # 
        ser = serial.serial_for_url('loop://', baudrate=115200, timeout=1)
        threading.Thread(target=feeder, args=(ser,), daemon=True).start()
        reader(ser,False)
    except KeyboardInterrupt:
        print("Exiting serial read/write.")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
    finally:
        if ser and ser.is_open:
            print("closing serial connection")
            ser.close()
        print("Serial connection closed")

    