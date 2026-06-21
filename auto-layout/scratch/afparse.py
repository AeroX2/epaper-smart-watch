import re, xml.etree.ElementTree as ET
F = r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX\db\mcu\IP\GPIO-STM32WB5Mx_gpio_v1_0_Modes.xml"
raw = open(F, encoding='utf-8').read()
# strip namespace for easy parsing
raw = re.sub(r'\sxmlns(:\w+)?="[^"]+"', '', raw)
raw = re.sub(r'ns0:', '', raw)
root = ET.fromstring(raw)

pin2sig = {}   # pin -> list of (signal, af)
sig2pin = {}   # signal -> list of (pin, af)
for gp in root.findall('.//GPIO_Pin'):
    pin = gp.get('Name')
    for ps in gp.findall('PinSignal'):
        sig = ps.get('Name')
        af = None
        pv = ps.find('.//PossibleValue')
        if pv is not None and pv.text and pv.text.startswith('GPIO_AF'):
            m = re.match(r'GPIO_AF(\d+)_', pv.text)
            af = int(m.group(1)) if m else None
        pin2sig.setdefault(pin, []).append((sig, af))
        sig2pin.setdefault(sig, []).append((pin, af))

# Module-exposed pins (from netlist U4 pin map) and which are FREE right now
exposed = set("""PA0 PA1 PA2 PA3 PA4 PA5 PA6 PA7 PA8 PA9 PA10 PA11 PA12 PA13 PA14 PA15
PB2 PB3 PB4 PB5 PB6 PB7 PB8 PB9 PB10 PB11 PB12 PB13 PB14 PB15
PC0 PC1 PC2 PC3 PC4 PC5 PC6 PC7 PC8 PC9 PC10 PC11 PC12 PC13
PD0 PD1 PD2 PD3 PD4 PD5 PD6 PD7 PD8 PD9 PD10 PD11 PD12 PD13 PD14 PD15
PE0 PE1 PE2 PE3 PE4 PH0 PH1""".split())
used = {  # pin: current function (from netlist)
 'PA2':'FLASH_CS','PA3':'FLASH_CLK','PA6':'FLASH_IO2','PA7':'FLASH_IO3','PB9':'FLASH_IO0','PB8':'FLASH_IO1',
 'PA4':'EPD_CSB','PA5':'EPD_SPICL','PA8':'EPD_DC',
 'PC1':'Accel_INT1','PC0':'Accel_INT2','PA15':'VIB(motor)',
 'PC10':'BTN1','PC11':'BTN2','PC12':'BTN3','PB4':'BTN4',
 'PA13':'SWDIO','PA14':'SWCLK','PA12':'USB_DP','PA11':'USB_DM',
 'PD0':'CHGR_ISET','PD1':'CHGR_MODE','PC6':'CHGR_SHPHLD',
 'PB14':'TOUCH(J2)','PB13':'TOUCH(J4)','PB12':'TOUCH(J5)',
 'PB10':'cap C25','PB11':'cap C26','PC5':'cap C22','PB15':'cap C19',
}
free = sorted(exposed - set(used), key=lambda p:(p[1], int(p[2:])))

def opts(signal, only_free=True):
    out=[]
    for pin,af in sig2pin.get(signal,[]):
        if pin in exposed and (not only_free or pin in free):
            out.append(f"{pin}(AF{af})")
    return out

print("FREE module pins:", ", ".join(free))
print()
for s in ['SPI1_SCK','SPI1_MISO','SPI1_MOSI','SPI1_NSS',
          'SPI2_SCK','SPI2_MISO','SPI2_MOSI','SPI2_NSS',
          'QUADSPI_CLK','QUADSPI_BK1_NCS','QUADSPI_BK1_IO0','QUADSPI_BK1_IO1','QUADSPI_BK1_IO2','QUADSPI_BK1_IO3',
          'I2C1_SCL','I2C1_SDA','I2C3_SCL','I2C3_SDA',
          'TIM1_CH1','TIM1_CH2','TIM1_CH3','TIM2_CH1','TIM2_CH2','TIM2_CH3','TIM2_CH4',
          'TIM16_CH1','TIM17_CH1','LPTIM1_OUT','LPTIM2_OUT']:
    allp = [f"{p}(AF{a})" for p,a in sig2pin.get(s,[]) if p in exposed]
    freep = opts(s)
    print(f"{s:18} free: {', '.join(freep) if freep else '-':45} | all: {', '.join(allp)}")

print("\n-- QUADSPI full (verify IO2/IO3) --")
for s in ['QUADSPI_BK1_IO2','QUADSPI_BK1_IO3']:
    print(s, [f"{p}(AF{a})" for p,a in sig2pin.get(s,[]) if p in exposed])
print("\n-- is PA15 a timer pin? --")
print('PA15:', [x for x in pin2sig.get('PA15',[]) if 'TIM' in x[0]])
