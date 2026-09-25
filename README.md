# Unnamed Gpl3 Qt App

Stuff I wanted to put in [Genexis](https://github.com/ExposureMG/Genexis) but I can't because it's GPL V3


# Heavy WIP


Planned:

- Qt GUI File Browser
- Xbox 360 formats supported

- Xbdm Client?
- XRPC / JRPC / JRPC2 Client?


FS Formats:

- Xbox 360 Neighbourhood
- FATX (Integrated FATX)
- STFS (Via libgxbuild)
- NAND (Via libgxbuild)
- xboxupd.bin (Via libgxbuild)

File Formats:

- XEX (Integrated XexTool)
- Bootloader? (Via libgxbuild)

Data Formats

- SMC Config
- DashLaunch ini
- RGLoader ini


FS Functions:

- Inject
- Extract
- Replace
- Delete
- Clear
- Health Check
- Attempt Repair


Health Check:

- STFS, XEX, Bootloader: Sha1 and RSA?
- NAND: ECD

Repair:

- STFS, XEX, Bootloader: N/A
- NAND: ECC


File Functions:

- Decrypt key [+ CPU key]
- Decompress
- Compress
- Encrypt: key [+ CPU key]
- Derive key: cpu and 1bl key
- Patch: offset and bin

File Info:

- Payload Sha1 (No Header)
- Crypt Status
- Compression Status

