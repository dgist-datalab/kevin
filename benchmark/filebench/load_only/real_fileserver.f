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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

set $dir=/mnt/ext4
set $nfiles=800000
set $meandirwidth=100
#set $filesize=cvar(type=cvar-gamma,parameters=mean:131072;gamma:1.5)
set $filesize=131072
set $nthreads=50
set $iosize=1m
set $meanappendsize=16k
set $count=8000000

set mode quit alldone

#set $fileidx=cvar(type=cvar-gamma,parameters=mean:10000;gamma:1.5,min=0,max=19999)

define fileset name=bigfileset,path=$dir,size=$filesize,entries=$nfiles,dirwidth=$meandirwidth,prealloc=80,paralloc

echo  "File-server Version 3.0 personality successfully loaded"

run 100000000
