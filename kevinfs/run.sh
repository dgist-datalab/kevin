insmod ./lightfs.ko
touch dummy.dev
sudo losetup /dev/loop3 dummy.dev
sudo mount -t lightfs /dev/loop3 $1
#sudo mount -t lightfs nodev $1
