needed `-O` to use the "legacy SCP protocol" to talk to dropbear with more recent operating system default ssh libraries.

scp -O -P 10022 root@127.0.0.1:/tmp/assignment4-result.txt ./assignment4/
