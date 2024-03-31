# Split
a directory / file splitter designed to split a file or a directory into fixed sized chunks

# Usage

```
$ ./build/split.exe

--split  [-n] [-r] [--size <split_size>] [--name <name>] <dir/file>
                 split a directory/file
                 symlinks WILL NOT be followed
                 the split files will be created in the current working directory
         -n
                 form the split map only, does not store any content
         -r
                 remove each directory/file upon being stored
                 if -n is given, no directories/files will be removed
         --size
                 specifies the split size, the default is 4 MB
                 if a value of zero is specified then the default of 4 MB is used
                 if this options is not specified then the default of 4 MB is used
         --name
                 specifies the prefix to be added to split.* files
                 if this options is not specified then an empty prefix is used
         <dir/file>
                 directory/file to split

--join   [-n] [-r] [prefix.]split.map --out <out_dir>
                 join a split map to restore a directory/file
         -n
                 list what would have been done, do not create/modify anything
         -r
                 remove each *split.* upon its contents being extracted
                 if -n is given, no *split.* files will be removed
         [prefix.]
                 an optional prefix for the split map
         --out
                 the directory to restore a directory/file into
                 defaults to the current directory

--ls     [prefix.]split.map
                 list the contents of a split map
         [prefix.]
                 an optional prefix for the split map
         <out_dir>
                 the directory to restore a directory/file into
                 defaults to the current directory

```