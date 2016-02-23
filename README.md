# [secc](https://github.com/ivere27/secc) native(c++) frontend
a project of 'Second Compiler'.

## Key Features
- best performance for distributed compilation
- completely working in memory (without any disk I/O)

## How to use
you may need to install dependencies by,
<br>ubuntu $ sudo apt-get install libcpprest-dev libssl-dev zlib1g-dev
```sh
git clone https://github.com/ivere27/secc-native.git
make
```

export SECC_ADDRESS, SECC_PORT
```sh
export SECC_ADDRESS=172.17.42.1
#(optional) export SECC_PORT=10509
```

then, (or export PATH=/path/to/secc-native/bin:$PATH)
```sh
# (optional) DEBUG=* SECC_LOG=/tmp/secc.log \
/path/to/secc-native/bin/clang -c test.c
```
