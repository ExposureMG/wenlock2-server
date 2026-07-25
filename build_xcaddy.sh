git clone https://github.com/caddyserver/caddy.git
cp Dockerfile caddy/ && cd caddy
docker build -t exposuremg9936/caddy:latest .