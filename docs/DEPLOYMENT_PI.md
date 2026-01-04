# Deployment on Raspberry Pi

## Install Docker
```bash
sudo apt update
sudo apt install -y docker.io docker-compose-plugin
sudo usermod -aG docker $USER
newgrp docker
```

## Run stack
```bash
cd /opt/wind
docker compose up -d
docker compose logs -f receiver
```

## Grafana
Open `http://<pi-ip>:3000`, add InfluxDB datasource, import `gateway/grafana/wind_dashboard.json`.
