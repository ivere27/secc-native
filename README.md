# [secc](https://github.com/ivere27/secc) native(c++) frontend
a project of 'Second Compiler'.

## Key Features
- best performance for distributed compilation
- completely working in memory (without any disk I/O)

## How to use
```sh
git clone https://github.com/ivere27/secc-native.git
make
```

export SECC_SCHEDULER_ADDRESS, SECC_SCHEDULER_PORT
```sh
export SECC_SCHEDULER_ADDRESS=172.17.42.1
export SECC_SCHEDULER_PORT=10509
```

then, (or export PATH=/path/to/secc-native/bin:$PATH)
```sh
# (optional) DEBUG=* \
/path/to/secc-native/bin/clang -c test.c
```
