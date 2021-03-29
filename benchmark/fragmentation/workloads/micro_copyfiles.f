#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

set $dir=/mnt/ext4
set $nfiles=2000000
set $meandirwidth=100
set $meanfilesize=4k
set $iosize=1m
set $nthreads=16
set $count=14000000

set mode quit firstdone

#set $fileidx=cvar(type=cvar-gamma,parameters=mean:40000;gamma:1.5,min=0,max=79999)

define fileset name=bigfileset,path=$dir,size=$meanfilesize,entries=$nfiles,dirwidth=$meandirwidth,prealloc=100,paralloc
define fileset name=destfiles,path=$dir,size=$meanfilesize,entries=$nfiles,dirwidth=$meandirwidth

define process name=filereader,instances=1
{
  thread name=filereaderthread,memsize=10m,instances=$nthreads
  {
    flowop openfile name=openfile1,filesetname=bigfileset,fd=1
    flowop readwholefile name=readfile1,fd=1,iosize=$iosize
    flowop createfile name=createfile2,filesetname=destfiles,fd=2
    flowop writewholefile name=writefile2,fd=2,iosize=$iosize
    flowop closefile name=closefile1,fd=1
    flowop closefile name=closefile2,fd=2
    flowop finishoncount name=finish,value=$count
  }
}

run 100000000

echo  "Copyfiles Version 3.0 personality successfully loaded"
