/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compile the production CNG/RC4 function without WinRT, controller or GATT.
 * All keys and packets below are public synthetic fixtures. */
#define GOVEE_BLE_CRYPTO_TEST
#include "../GoveeBluetoothController_Windows.cpp"
#include <cassert>
#include <iostream>

int main()
{
    using namespace GoveeBluetooth;
    Key key{};
    for(unsigned int i=0;i<16;++i) key[i]=static_cast<uint8_t>(i);
    // FIPS197 AppendixC AES128 block + independent RC4 key00..0f fixture.
    const Packet plain{{0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0,0,0,0}};
    const Packet expected{{0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a,0xe9,0x9c,0x40,0xf9}};
    assert(Crypt(plain,key,false)==expected);
    assert(Crypt(expected,key,true)==plain);
    std::cout << "PASS independent AES128 ECB and RC4-tail known-answer vector\n";
    for(unsigned int n=0;n<256;++n)
    {
        Packet packet=MakePacket(0x33,5,{5,0,static_cast<uint8_t>(n),100,50});
        assert(Crypt(Crypt(packet,key,false),key,true)==packet);
    }
    std::cout << "PASS 256 synthetic packet encryption/decryption round-trips\n";
}
