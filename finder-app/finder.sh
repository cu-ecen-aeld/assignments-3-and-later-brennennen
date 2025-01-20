#!/bin/bash

print_usage() {
    echo "usage: finder.sh {search_dir} {search_str}"
}

# exits with return value 1 error and print statements if any of the parameters were not specified
#echo "args count: $#"
if [ $# -ne 2 ]
then
    echo "Missing arguments!"
    print_usage
    exit 1
fi

# first arg is a path to a directory on the filesystem
files_dir=$1
# second arg is a text string which will be searched within these files
search_str=$2
#echo "files_dir: '$files_dir', search_str: '$search_str'"

# exits with return value 1 error and print statements if filesdir does not exist
if [ ! -d $files_dir ]
then
    echo "Directory to search does not exist!"
    print_usage
    exit 1
fi

# print a message "the number of files are X and the number of matching lines are Y"
files_found=$(find $files_dir -type f | wc -l)
matching_lines=$(grep -r $search_str $files_dir | wc -l)
echo "The number of files are $files_found and the number of matching lines are $matching_lines"
