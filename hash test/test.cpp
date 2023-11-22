#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <climits>
#include "crc_tables.h"

/* This code was adapted from:
   http://www.barrgroup.com/Embedded-Systems/How-To/CRC-Calculation-C-Code */

template <typename T>
T reflect(T data, int nBits) {
  T reflection = static_cast<T>(0x00);
  int bit;

  // Reflect the data about the center bit.
  for (bit = 0; bit < nBits; ++bit) {
    // If the LSB bit is set, set the reflection of it.
    if (data & 0x01) {
      reflection |= (static_cast<T>(1) << ((nBits - 1) - bit));
    }
    data = (data >> 1);
  }

  return reflection;
}


uint16_t crc16(const char *buf, size_t len){
    uint16_t remainder = 0x0000;
    uint16_t final_xor_value = 0x0000;
    for (unsigned int byte = 0; byte < len; byte++) {
      int data = reflect<uint16_t>(buf[byte], 8) ^ (remainder >> 8);
      remainder = table_crc16[data] ^ (remainder << 8);
    }
    return reflect<uint16_t>(remainder, 16) ^ final_xor_value;
}

uint32_t crc32 (const char *buf, size_t len){
    uint32_t remainder = 0xFFFFFFFF;
    uint32_t final_xor_value = 0xFFFFFFFF;
    for (unsigned int byte = 0; byte < len; byte++) {
      int data = reflect<uint32_t>(buf[byte], 8) ^ (remainder >> 24);
      remainder = table_crc32[data] ^ (remainder << 8);
    }
    return reflect<uint32_t>(remainder, 32) ^ final_xor_value;
}


uint16_t cksum16(const char *buf, size_t len){
    uint64_t sum = 0;
    const uint64_t *b = reinterpret_cast<const uint64_t *>(buf);
    uint32_t t1, t2;
    uint16_t t3, t4;
    const uint8_t *tail;
    /* Main loop - 8 bytes at a time */
    while (len >= sizeof(uint64_t)) {
      uint64_t s = *b++;
      sum += s;
      if (sum < s) sum++;
      len -= 8;
    }
    /* Handle tail less than 8-bytes long */
    tail = reinterpret_cast<const uint8_t *>(b);
    if (len & 4) {
      uint32_t s = *reinterpret_cast<const uint32_t *>(tail);
      sum += s;
      if (sum < s) sum++;
      tail += 4;
    }
    if (len & 2) {
      uint16_t s = *reinterpret_cast<const uint16_t *>(tail);
      sum += s;
      if (sum < s) sum++;
      tail += 2;
    }
    if (len & 1) {
      uint8_t s = *reinterpret_cast<const uint8_t *>(tail);
      sum += s;
      if (sum < s) sum++;
    }
    /* Fold down to 16 bits */
    t1 = sum;
    t2 = sum >> 32;
    t1 += t2;
    if (t1 < t2) t1++;
    t3 = t1;
    t4 = t1 >> 16;
    t3 += t4;
    if (t3 < t4) t3++;
    return t3;
}

uint16_t csum16(const char *buf, size_t len){
    return cksum16(buf, len);
}

uint16_t xor16(const char *buf, size_t len){
    uint16_t mask = 0x00ff;
    uint16_t final_xor_value = 0x0000;
    unsigned int byte = 0;
    uint16_t t1, t2;
    /* Main loop - 2 bytes at a time */
    while (len >= 2) {
      t1 = static_cast<uint16_t>(buf[byte]) << 8;
      t2 = static_cast<uint16_t>(buf[byte + 1]);
      final_xor_value = final_xor_value ^ (t1 + (t2 & mask));
      byte += 2;
      len -= 2;
    }
    if (len > 0) {
      t1 = static_cast<uint16_t>(buf[byte]) << 8;
      final_xor_value = final_xor_value ^ t1;
    }
    return final_xor_value;
}

uint64_t identity(const char *buf, size_t len){
    uint64_t res = 0ULL;
    for (size_t i = 0; i < std::min(sizeof(res), len); i++) {
      if (i > 0) res <<= 8;
      res += static_cast<uint8_t>(buf[i]);
    }
    return res;
}


enum Hash{CRC16, CRC32, XOR16, IDENTITY, CSUM16};
std::string HASH_METHOD[] = {"CRC16", "CRC32", "XOR16", "IDENTITY", "CSUM16"};



// std::string calculateHash(Hash algorithm) {
    
//     uint16_t res = 0;
//     if(algorithm == CRC32)
//         uint32_t res = 0;
//     else
//         uint64_t res = 0;
//         if(algorithm == CRC16)
//             res = crc16(buffer, file.gcount());
//         else if(algorithm == CRC32)
//             res = crc32(buffer, file.gcount());
//         else if(algorithm == XOR16)
//             res = xor16(buffer, file.gcount());
//         else if(algorithm == IDENTITY)
//             res = identity(buffer, file.gcount());
//         else
//             res = csum16(buffer, file.gcount());

//     }
//     std::string hashstring = std::to_string(res);
//     return hashstring;
// }

// double testHashAlgorithm(Hash algorithm) {
//     auto start = std::chrono::steady_clock::now();

//     std::string res = calculateHash(algorithm);

//     auto end = std::chrono::steady_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

//     return duration.count()/1000;
// }

#define BYTES_NUM 13
#define TEST_NUM 10000 //每个hash算法的哈希次数

std::unordered_map<uint32_t, int> CRC32Counts;
std::unordered_map<uint16_t, int> CRC16Counts;
std::unordered_map<uint64_t, int> IDENTITYCounts;
std::unordered_map<uint16_t, int> XOR16Counts;
std::unordered_map<uint16_t, int> CSUM16Counts;
double calculateAndLogHash(const char* data, size_t dataSize, Hash algorithm) {
    std::ofstream logFile("hash_speed_test_Bytes.log", std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Error opening log file." << std::endl;
        return 0;
    }

    uint16_t CRC16res = 0;
    uint32_t CRC32res = 0;
    uint16_t XOR16res = 0;
    uint64_t Identityres = 0;
    uint16_t CSUM16res = 0;

    auto start = std::chrono::steady_clock::now();
    if(algorithm == CRC16)
      CRC16res = crc16(data, BYTES_NUM);
    else if(algorithm == CRC32){
      CRC32res = crc32(data, BYTES_NUM);
    }
    else if(algorithm == XOR16)
      XOR16res = xor16(data, BYTES_NUM);
    else if(algorithm == IDENTITY){
      Identityres = identity(data, BYTES_NUM);}
    else
      CSUM16res = csum16(data, BYTES_NUM);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    if(algorithm == CRC32)
      ++CRC32Counts[CRC32res];
    else if(algorithm == IDENTITY)
      ++IDENTITYCounts[Identityres];
    else if(algorithm == CRC16)
      ++CRC16Counts[CRC16res];
    else if(algorithm == XOR16)
      ++XOR16Counts[XOR16res];
    else
      ++CSUM16Counts[CSUM16res];

    logFile << "Hash Algorithm: "
            << HASH_METHOD[algorithm]
            << "\nTime taken: " << duration.count() << " nanoseconds\n"
            << "-------------------------------------------\n";

    logFile.close();
    return duration.count();
}

double CollisionRate(Hash algorithm){
  if(algorithm == CRC32){
    int numCRC32Collisions = 0;
    for (const auto& pair : CRC32Counts) {
        if (pair.second > 1) {
            numCRC32Collisions += pair.second - 1;
        }
    }
    return static_cast<double>(numCRC32Collisions) / TEST_NUM;
  }
  else if(algorithm == IDENTITY){
    int numIdentityCollisions = 0;
    for (const auto& pair : IDENTITYCounts) {
        if (pair.second > 1) {
            numIdentityCollisions += pair.second - 1;
        }
    }
    return static_cast<double>(numIdentityCollisions) / TEST_NUM;
  }
  else if(algorithm == CRC16){
    int numCRC16Collisions = 0;
    for (const auto& pair : CRC16Counts) {
        if (pair.second > 1) {
            numCRC16Collisions += pair.second - 1;
        }
    }
    return static_cast<double>(numCRC16Collisions) / TEST_NUM;
  }
  else if(algorithm == XOR16){
    int numXOR16Collisions = 0;
    for (const auto& pair : XOR16Counts) {
        if (pair.second > 1) {
            numXOR16Collisions += pair.second - 1;
        }
    }
    return static_cast<double>(numXOR16Collisions) / TEST_NUM;
  }
  else{
    int numCSUM16Collisions = 0;
    for (const auto& pair : CSUM16Counts) {
        if (pair.second > 1) {
            numCSUM16Collisions += pair.second - 1;
        }
    }
    return static_cast<double>(numCSUM16Collisions) / TEST_NUM;
  }
}

int main() {
  char randomData[BYTES_NUM];
  for (int i = static_cast<int>(Hash::CRC16); i <= static_cast<int>(Hash::CSUM16); ++i) {

    double avgTime = 0;
    Hash algorithm = static_cast<Hash>(i);
    for(int j = 0;j < TEST_NUM; j++){
      for (size_t i = 0; i < BYTES_NUM; ++i) {
        randomData[i] = static_cast<unsigned char>(rand() % 256); // 生成随机字节
      }
      avgTime += calculateAndLogHash(randomData, BYTES_NUM, algorithm)/TEST_NUM;
    }
    double Collision_Rate = CollisionRate(algorithm);
    std::cout << "Hash method:" << HASH_METHOD[algorithm] << '\n' << "Average time :" << avgTime << " nanoseconds" << '\n' << "Collision Rate : " << Collision_Rate << '\n';
  }
  return 0;
}