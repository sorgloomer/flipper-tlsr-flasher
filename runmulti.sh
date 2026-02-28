set -euxo pipefail

python dump.py --dump --no-short --bitrate 300000 --chunksize 8192
