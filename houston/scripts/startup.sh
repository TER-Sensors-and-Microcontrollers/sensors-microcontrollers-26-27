
# wait until IP is assigned
while ! hostname -I > /dev/null 2>&1; do
    echo "Waiting for network..."
    sleep 1
done
# Absolute paths for scripts might need to change
# depending on whether this is run in a container
# or on the Raspberry Pi
echo "Starting processes..."
python3 /workspace/serial-reader.py & 
python3 /workspace/app.py &