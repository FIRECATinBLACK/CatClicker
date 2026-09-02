#pragma once

#include <QtCore/QString>

#include <cstdint>

namespace CatClicker::VirtualDeviceIdentity {

inline constexpr uint16_t VendorId = 0xCA7C;
inline constexpr uint16_t KeyboardProductId = 0x1001;
inline constexpr uint16_t PointerProductId = 0x1002;

inline const char *KeyboardName = "CatClicker Virtual Keyboard";
inline const char *PointerName = "CatClicker Virtual Pointer";

bool isCatClickerVirtualDevice(const QString &name, uint16_t vendorId, uint16_t productId);
bool isCatClickerVirtualDeviceName(const QString &name);

}
