"""Private offline fixture; inputs are owned game executables, never committed."""
import pathlib, struct, sys
import pefile
current=pefile.PE(sys.argv[1]); old=pefile.PE(sys.argv[2])
image=current.get_memory_mapped_image()
historical=old.get_data(0x59CA0,0xAC)
assert historical[:8]==bytes.fromhex('558beca1e0816400')
assert historical[-3:]==bytes.fromhex('c21400')
assert current.get_data(0xF9F60,9)==bytes.fromhex('558bec51a1f010d400')
out=pathlib.Path(sys.argv[3])
out.write_bytes(struct.pack('<8sIIII',b'HCGQ512\0',1,current.OPTIONAL_HEADER.ImageBase,len(image),len(historical))+image+historical)
print('Extracted private graphics fixture:',out)
