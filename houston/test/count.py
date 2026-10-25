# count.py
# prints how many readings are in the database and the most recent few
import os, sqlite3

db_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "database.db")
db = sqlite3.connect(db_path)

total = db.execute("SELECT COUNT(*) FROM sensor_readings").fetchone()[0]
print("Total readings:", total)

for row in db.execute("SELECT sensor_id, name, data, unit FROM sensor_readings ORDER BY reading_id DESC LIMIT 5"):
    print(row)
