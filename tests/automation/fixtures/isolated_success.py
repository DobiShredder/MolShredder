import sys


print(f"isolated-argument={sys.argv[1]}")
print("isolated-stderr", file=sys.stderr)
