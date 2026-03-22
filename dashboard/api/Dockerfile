# Use an official Python runtime as a parent image
FROM python:3.14-slim

# Set the working directory in the container
WORKDIR /app

RUN apt-get update
RUN apt-get install -y gcc build-essential zlib1g-dev curl

# Copy the requirements file into the container
COPY requirements.txt .

# Install any needed packages specified in requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

# Copy only the necessary application code into the container
COPY simulation.py .
COPY db.py .
COPY models.py .
COPY migrations.sql .
COPY ws_manager.py .

# Note: For production, consider using Docker secrets or environment variables
# instead of copying a .env file directly into the image.
# If .env is strictly needed for local development within the container,
# you would add: COPY .env .

# Make port 8000 available to the world outside this container
EXPOSE 8000

# Run simulation.py when the container launches
# --forwarded-allow-ips='*' allows connections from any IP (needed for host WebSocket connections)
CMD ["uvicorn", "simulation:app", "--host", "0.0.0.0", "--port", "8000", "--forwarded-allow-ips", "*"]

