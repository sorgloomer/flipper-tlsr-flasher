set -euxo pipefail

python tool.py --debug --dump --no-short --bitrate 300000 --chunksize 8192
