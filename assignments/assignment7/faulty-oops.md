```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b6c000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: faulty(O) scull(O) hello(O) [last unloaded: scull(O)]
CPU: 0 PID: 113 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008db3d20
x29: ffffffc008db3d80 x28: ffffff8001b9b500 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008db3dc0
x20: 000000555db18240 x19: ffffff8001bdc100 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc00078c000 x3 : ffffffc008db3dc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

# Kernel Oops Analysis - Assignment 7

## Oops Trigger

The oops was triggered by writing to the `/dev/faulty` device:

```
bash
echo "hello_world" > /dev/faulty
```

## Oops Summary

```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
```

# Key Information from the Oops Message

## What Happened

- Error Type: Null pointer dereference at virtual address `0x0000000000000000`
- Memory Access: Write operation (WnR = 1 Data abort info)
- Exception Level: Current EL (Exception Level) indicating kernel space

## Where it Occurred

- Module: `faulty` (marked with `[faulty]`)
- Function: `faulty_write`
- Instruction Pointer: `pc : faulty_write+0x10/0x20 [faulty]`
    - The crash occured 16 bytes (0x10) into the faulty_write function
    - The total function size is 32 bytes (0x20)

 ## Call Trace Analysis

 The call trace shows the complete execution path

 ```
faulty_write+0x10/0x20 [faulty]
ksys_write+0x74/0x110
__arm64_sys_write+0x1c/0x30
invoke_syscall+0x54/0x130
el0_svc_common.constprop.0+0x44/0xf0
do_el0_svc+0x2c/0xc0
el0_svc+0x2c/0x90
el0t_64_sync_handler+0xf4/0x120
el0t_64_sync+0x18c/0x190
```

It shows the write system call going from userspace through the kernel to the faulty driver

## How to Locate the Faulty Code

1. Find the module `faulty.ko`
2. Find the function: `faulty_write`
3. Locate the offset: 0x10 bytes into the function
4. Use objdump: `objdump -S faulty.ko` to see the assembly with source interleaving
5. Look for the null pointer access: The instruction at offset 0x10 is likely holding a null pointer


# Conclusion

The oops message has all the neccessary info to find the exact line of code causing the null pointer dereference in the `faulty_write` function of the `faulty` kernel module.