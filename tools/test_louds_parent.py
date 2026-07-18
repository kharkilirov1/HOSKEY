import struct

def count_bits(v):
    c = 0
    while v:
        v &= v - 1
        c += 1
    return c

class BitVector:
    def __init__(self, data):
        self.words = struct.unpack(f"<{len(data)//8}Q", data)
        self.rank1_cache = [0] * (len(self.words) + 1)
        for i in range(len(self.words)):
            self.rank1_cache[i+1] = self.rank1_cache[i] + count_bits(self.words[i])
            
    def get_bit(self, i):
        w = i // 64
        b = i % 64
        return (self.words[w] >> b) & 1
        
    def rank1(self, i):
        w = i // 64
        b = i % 64
        ans = self.rank1_cache[w]
        if b > 0:
            mask = (1 << b) - 1
            ans += count_bits(self.words[w] & mask)
        return ans
        
    def rank0(self, i):
        return i - self.rank1(i)
        
    def select1(self, n):
        if n == 0: return -1
        # simple linear search
        for i in range(len(self.words) * 64):
            if self.get_bit(i):
                n -= 1
                if n == 0: return i
        return -1
        
    def parent(self, node_id):
        # node_id corresponds to the node_id-th 1-bit (1-based index is node_id)
        if node_id == 0: return -1
        pos = self.select1(node_id)
        if pos == -1: return -1
        return self.rank0(pos)

def test_trie():
    with open(r"C:\Users\Kharki\Desktop\yandex_extracted\extracted_sections\trie", "rb") as f:
        # Read BV0 (main louds) at 130744.
        f.seek(130744)
        magic, size, ones, _ = struct.unpack("<IIII", f.read(16))
        bv_data = f.read(size * 8)
    bv = BitVector(bv_data)
    
    # Read labels at 0x110000? Let's check where labels are.
    # The file is 4.3 MB. Labels are usually after bitvectors.
    # In decode_1nc7_final.py, labels are right after BV3.
    # We can just check the first 20 nodes.
    print("Testing LOUDS parent formula:")
    for i in range(1, 20):
        print(f"Node {i} -> Parent {bv.parent(i)}")

if __name__ == "__main__":
    test_trie()
