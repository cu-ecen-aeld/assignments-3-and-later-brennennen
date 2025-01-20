#!/bin/bash

print_usage() {
    echo "usage: writer.sh {write_file} {write_str}"
}

# exits with value 1 error and print statements if any of the arguments are not specified
if [ $# -ne 2 ]
then
    echo "Missing arguments!"
    print_usage
    exit 1
fi

# first arg is a full path to a file
write_file=$1
write_str=$2

# creates a new file with name and path writefile and content writestr, overwrite if exists and
# create path if not exist. Exit with 1 and print error if anything fails.
dir_name=$(dirname ${write_file})
mkdir -p $dir_name
if ! mkdir -p $dir_name
then
    echo "Failed to make directory '$dir_name'."
    exit 1
fi

touch $write_file
echo $write_str > $write_file
exit 0
