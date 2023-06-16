#!/bin/sh

root_dir=$(cd `dirname $0`; pwd)
root_dir=`dirname $root_dir`
os=`uname`

#source ${root_dir}/scripts/common.sh

cpp_dirs=" \
	$root_dir/slark/base/headers \
	$root_dir/slark/base/src \
	$root_dir/slark/base/test \
	$root_dir/slark/core/audio \
	$root_dir/slark/core/headers \
	$root_dir/slark/core/src \
"

ASTYLE=$(which astyle)

if [ "$?" != "0" ]; then
	echo "please install Artistic Style."
else
    echo "Artistic Style path:" $ASTYLE 
fi

ini_path="${root_dir}/script/slark.ini"

# c++
for dir in $cpp_dirs; do
	$ASTYLE --options=$ini_path -n -r "$dir/*.hpp"
	$ASTYLE --options=$ini_path -n -r "$dir/*.cpp"
done

exit 0
