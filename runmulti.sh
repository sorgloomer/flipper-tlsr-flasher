set -euxo pipefail

python tool.py --dump --no-short --bitrate 300000 --chunksize 8192
