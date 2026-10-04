#include <iostream>
#include <string>

#include "molshredder/core/sha256.hpp"

int main() {
  using molshredder::core::sha256_hex;
  struct Vector {
    std::string input;
    const char* digest;
  };
  const Vector vectors[]{
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
      {std::string(1000000, 'a'),
       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
      // 55 and 56 bytes straddle the padding boundary.
      {std::string(55, 'a'),
       "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
      {std::string(56, 'a'),
       "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
  };
  bool passed = true;
  for (const auto& vector : vectors) {
    if (sha256_hex(vector.input) != vector.digest) {
      std::cerr << "FAILED: sha256 of " << vector.input.size() << " bytes\n";
      passed = false;
    }
  }
  return passed ? 0 : 1;
}
