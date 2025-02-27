**LSASS** dump + HTTPS exfiltration, all in-memory.
![17405996115875858500834891757661](https://github.com/user-attachments/assets/4d23661c-e964-4dbb-ae9c-f7b0d5d2b27f)
How It works 
- Takes a snapshot of `lsass.exe` and then create a `MiniDump` of it.
- Obfuscates the `"lsass.exe"` string with XOR in source-code to avoid flagging.
- In-memory compression of MiniDump then uploads it to remote webserver via HTTPS.
