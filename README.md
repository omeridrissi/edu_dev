# EDU virtual PCI driver

## compilation

```bash
#build
make

#clean up
make clean
```

## use

```bash
# Load module
sudo insmod edu.ko
```

---

Write to `/dev/edu0` for control or storing dma buffer (max 100 bytes).

```c
write(fd, buf, buf_size);
```
where

`fd`: `/dev/edu0` file descriptor
`buf`: \[use_dma '0'/'1'\]\[        input(100 bytes)        \]
`buf_size`: size of buf

---

Read from `/dev/edu0` to get factorial computation or stored buffer (max 100 bytes)

```c
read(fd, buf, buf_size)
```

where

`fd`: `/dev/ed0` file descriptor
`buf`: empty 100 byte read buffer
`buf_size`: size of buf


