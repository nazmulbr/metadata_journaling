# VSFS Metadata Journaling Project

## Overview

This project implements **metadata journaling** for a very small educational file system called **VSFS (Very Simple File System)**.  
The goal is to ensure filesystem consistency by **logging metadata updates to a journal before applying them to disk**.

The **final and main executable is `journal.c`**, which provides two commands:

- `create <filename>` — logs creation of a file into the journal
- `install` — installs (replays) committed journal transactions into the filesystem

This project is **standalone** and **NOT part of xv6**.

---

## Files in the Project

### ✅ `journal.c` (MAIN FILE)

This is the heart of the project.

It:
- Opens and memory-maps `vsfs.img`
- Writes metadata updates into the journal (block 1)
- Supports **transaction logging**
- Replays committed transactions safely

Supported commands:
```bash
./journal create hello.txt
./journal install
```

---

### 🧱 Supporting Files

These files help test and validate the filesystem

#### `mkfs.c` / `mkfs`
- Creates a fresh `vsfs.img`
- Initializes:
  - Superblock
  - Inode bitmap
  - Data bitmap
  - Root directory
  - Empty journal

Usage:
```bash
./mkfs
```

---

#### `validator.c` / `validator`
- Verifies filesystem consistency
- Ensures:
  - Bitmap correctness
  - Inode references are valid
  - Directory structure is sane

Usage:
```bash
./validator
```

---

#### `vsfs.img`
- The disk image (85 blocks × 4096 bytes)
- Memory-mapped by `journal.c`
- Modified only after journal installation

---

## Filesystem Layout (Important)

| Block | Purpose |
|------|--------|
| 0 | Superblock |
| 1 | Journal |
| 17 | Inode Bitmap |
| 18 | Data Bitmap |
| 19–20 | Inode Table |
| 21 | Root Directory Data |

---

## How Journaling Works (journal.c)

### 1. Create Command
```bash
./journal create hello.txt
```

What happens:
1. Finds free inode using inode bitmap
2. Finds free directory entry
3. Prepares **metadata changes**
4. Writes them as **journal records**
5. Appends a **COMMIT record**
6. DOES NOT modify filesystem yet

---

### 2. Install Command
```bash
./journal install
```

What happens:
1. Reads journal records
2. Replays metadata blocks to disk
3. Applies changes atomically
4. Clears the journal

---

## Journal Record Types

| Type | Meaning |
|----|-------|
| `REC_DATA` | Metadata block update |
| `REC_COMMIT` | Transaction boundary |

---

## Compilation & Execution

```bash
gcc journal.c -o journal
./mkfs
./validator
./journal create hello.txt
./journal install
./validator
```

---

## Expected Output

```text
Created VSFS image 'vsfs.img' (85 blocks).
Filesystem 'vsfs.img' is consistent.
Logged creation of 'hello.txt' to journal.
Installed 1 committed transactions from journal.
```

---

## Key Concepts Demonstrated

- Memory-mapped disk I/O (`mmap`)
- Write-ahead logging
- Crash-safe metadata updates
- Bitmap-based resource management
- Transaction commit semantics

---

## Notes

- Only **metadata** is journaled
- File data blocks are not implemented
- This is an educational filesystem

---

## Author

Nazmul Haque  
Metadata Journaling Project

