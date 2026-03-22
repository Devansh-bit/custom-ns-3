import psycopg
from dotenv import load_dotenv
import os

load_dotenv()

PASSWORD = os.environ["DB_PASS"]


def run_migrations(host, dbname: str = "extension_box"):
    with psycopg.connect(
        f"host={host} user=postgres dbname={dbname} password={PASSWORD}"
    ) as conn:
        print("Connected to database successfully")

        with open("./migrations.sql") as m:
            data = m.read()
            conn.execute(data)
        print("Migration successful")


if __name__ == "__main__":
    run_migrations()
