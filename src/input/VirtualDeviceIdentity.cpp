#include "VirtualDeviceIdentity.h"

namespace CatClicker::VirtualDeviceIdentity {

bool isCatClickerVirtualDevice(const QString &name, uint16_t vendorId, uint16_t productId)
{
    return isCatClickerVirtualDeviceName(name)
        && vendorId == VendorId
        && (productId == KeyboardProductId || productId == PointerProductId);
}

bool isCatClickerVirtualDeviceName(const QString &name)
{
    return name.startsWith(QStringLiteral("CatClicker Virtual"));
}

}
