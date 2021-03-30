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
# Creates a fileset with $ndirs empty leaf directories then rmdir's all of them
#
set $dir=/bench
set $ndirs=8000000
set $meandirwidth=100
set $nthreads=16
set $count=8000000

set mode quit alldone

set $fileidx=cvar(type=cvar-gamma,parameters=mean:4000000;gamma:1.5,min=0,max=7999999)

define fileset name=bigfileset1,path=$dir,size=0,leafdirs=$ndirs,dirwidth=$meandirwidth,prealloc,paralloc

define process name=remdir,instances=1
{
  thread name=removedirectory,memsize=1m,instances=$nthreads
  {
    flowop removedir name=dirremover,filesetname=bigfileset1,indexed=$fileidx
    flowop opslimit name=limit
    flowop finishoncount name=finish,value=$count
  }
}

run 100000000

echo  "RemoveDir Version 1.0 personality successfully loaded"
