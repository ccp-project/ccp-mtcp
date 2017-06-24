ETH=$1

sudo tc qdisc del dev ifb0 root
sudo tc qdisc del dev $ETH ingress
