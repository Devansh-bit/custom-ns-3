import subprocess
import os
import sys

simulation_port = 8001

custom_env = os.environ.copy()


def start_dockerc_compose(config_file: str = "./config/config-simulation.json"):
    global simulation_port
    # try:
    # Restart docker-compose (this will stop and start containers)
    custom_env["API_PORT"] = str(simulation_port)
    custom_env["DB_NAME"] = f"sim_{simulation_port}"
    custom_env["KAFKA_TOPIC"] = f"sim_{simulation_port}"
    custom_env["CONFIG_FILE"] = config_file

    subprocess.run(
        [
            "docker",
            "compose",
            "-f",
            "../docker-compose-sim.yml",
            f"-p={simulation_port}",
            "up",
            "-d",
        ],
        stderr=sys.stderr,
        stdout=sys.stdout,
        env=custom_env,
    )

    simulation_port += 1


def stop_simulation(sim_id: int):
    subprocess.run(
        [
            "docker",
            "compose",
            "-f",
            "../docker-compose-sim.yml",
            f"-p={sim_id}",
            "down",
        ],
        stderr=sys.stderr,
        stdout=sys.stdout,
        env=custom_env,
        timeout=60,
    )


def stop_all_simulations():
    global simulation_port
    for n in range(simulation_port, 8000, -1):
        subprocess.run(
            [
                "docker",
                "compose",
                "-f",
                "../docker-compose-sim.yml",
                f"-p={n}",
                "down",
            ],
            env=custom_env,
            timeout=60,
        )
