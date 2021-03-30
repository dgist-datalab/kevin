#echo $$ > /sys/fs/cgroup/bench/cgroup.procs; filebench -f tmp/copyfiles_16_gamma_fuse.f
#echo $$ > /sys/fs/cgroup/bench/cgroup.procs; filebench -f tmp/filemicro_createfiles_16_fuse.f
#filebench -f tmp/makedirs_16_fuse.f
#filebench -f tmp/removedirs_16_fuse.f
#filebench -f tmp/filemicro_createfiles_16_fuse.f
#filebench -f tmp/filemicro_delete_16_fuse.f
#filebench -f tmp/copyfiles_16_gamma_fuse.f
#filebench -f tmp/fileserver_gamma_fuse.f
#filebench -f tmp/varmail_gamma_fuse.f
#filebench -f tmp/webproxy_gamma_fuse.f
#filebench -f tmp/webserver_gamma_fuse.f
#filebench -f tmp/tmp.f
#filebench -f tmp/micro_makedirs.f
#filebench -f tmp/micro_removedirs.f
filebench -f tmp/micro_copyfiles.f
#filebench -f tmp/micro_makedirs.f
