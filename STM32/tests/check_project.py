"""Проверка целостности CubeMX/Keil проекта после генерации, без платы и HAL mock."""

from pathlib import Path
import re
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
STM32 = ROOT / "STM32"
PROJECT = STM32 / "MDK-ARM" / "oscill_controls.uvprojx"


def require(condition, message):
    """Остановить проверку с конкретной причиной, пригодной для лога сборки."""
    if not condition:
        raise AssertionError(message)


def user_sections(source):
    """Выделить сохраняемые CubeMX участки для проверки точек входа приложения."""
    pattern = r"/\* USER CODE BEGIN ([^*]+) \*/(.*?)/\* USER CODE END \1 \*/"
    return dict(re.findall(pattern, source, re.DOTALL))


def check_project():
    """Проверить связи конфигурации, приложения и Keil, которые может затронуть генерация."""
    config = {}
    for line in (STM32 / "oscill_controls.ioc").read_text(encoding="utf-8-sig").splitlines():
        if "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            config[key] = value

    expected = {
        "Mcu.CPN": "STM32F103RCT6",
        "Mcu.Package": "LQFP64",
        "ProjectManager.KeepUserCode": "true",
        "ProjectManager.CoupleFile": "true",
        "ProjectManager.MainLocation": "Core/Src",
        "ProjectManager.TargetToolchain": "MDK-ARM V5.32",
        "PA13.Signal": "SYS_JTMS-SWDIO",
        "PA14.Signal": "SYS_JTCK-SWCLK",
        "PA9.Signal": "USART1_TX",
        "PA10.Signal": "USART1_RX",
        "PC7.Signal": "GPIO_Output",
        "PC7.GPIO_Label": "LED_RUN",
        "PC7.PinState": "GPIO_PIN_RESET",
        "RCC.PLLSourceVirtual": "RCC_PLLSOURCE_HSI_DIV2",
        "RCC.PLLMUL": "RCC_PLL_MUL16",
        "RCC.APB1CLKDivider": "RCC_HCLK_DIV2",
        "PB1.Signal": "ADCx_IN9",
        "PB1.GPIO_Label": "ANALOG_VBAT",
        "SH.ADCx_IN9.0": "ADC1_IN9,IN9",
        "ADC1.SamplingTime-0\\#ChannelRegularConversion": "ADC_SAMPLETIME_239CYCLES_5",
    }
    for key, value in expected.items():
        require(config.get(key) == value, f"CubeMX: {key} must be {value}")
    for pin, label, high in (("PA3", "RELAY_A1", False), ("PA4", "RELAY_A2", True),
                             ("PA5", "RELAY_A3", True), ("PA2", "RELAY_B1", False),
                             ("PA1", "RELAY_B2", True), ("PA0-WKUP", "RELAY_B3", True)):
        require(config.get(pin + ".GPIO_Label") == label, f"Relay mapping lost: {label}")
        require(config.get(pin + ".Signal") == "GPIO_Output", f"Relay output lost: {pin}")
        require(config.get(pin + ".PinState") == ("GPIO_PIN_SET" if high else "GPIO_PIN_RESET"),
                f"Relay initial state changed: {pin}")
    require(config.get("NVIC.USART1_IRQn", "").startswith("true"), "USART1 IRQ disabled")

    target = ET.parse(PROJECT).find("./Targets/Target")
    require(target is not None, "Keil target missing")
    require(target.findtext("uAC6") == "1", "Arm Compiler 6 selection lost")
    require(target.findtext("./TargetOption/TargetCommonOption/Device") == "STM32F103RC",
            "Keil target device changed")

    options = ET.parse(PROJECT.with_suffix(".uvoptx")).getroot()
    driver = next((entry.findtext("Name", "") for entry in options.findall(".//SetRegEntry")
                   if entry.findtext("Key") == "ST-LINKIII-KEIL_SWO"), "")
    flash_options = re.search(r"(?:^|\s)-FO(\d+)\b", driver)
    require(flash_options is not None and int(flash_options[1]) & 15 == 15,
            "ST-Link Download must erase, program, verify, then reset and run")
    require(options.findtext(".//DebugDescription/Enable") == "0",
            "Reset and run must use the ST-Link driver, not the pack debug sequence")

    paths = set()
    for item in target.findall("./Groups/Group/Files/File/FilePath"):
        resolved = (PROJECT.parent / item.text.replace("\\", "/")).resolve()
        require(resolved.is_file(), f"Keil source missing: {item.text}")
        paths.add(resolved)

    for name in ("controls.c", "board_controls.c", "controls_uart.c", "oscill_main.c",
                 "panel_state.c", "board_outputs.c"):
        require((STM32 / "Core/Oscill" / name).resolve() in paths, f"Application source lost: {name}")
    require((ROOT / "ZYNQ7020/gui/src/control_protocol.c").resolve() in paths,
            "Shared codec source lost")
    require((STM32 / "Core/Src/adc.c").resolve() in paths, "ADC initialization source lost")
    for name in ("stm32f1xx_hal_adc.c", "stm32f1xx_hal_adc_ex.c"):
        require((STM32 / "Drivers/STM32F1xx_HAL_Driver/Src" / name).resolve() in paths,
                f"ADC driver source lost: {name}")
    for name in ("general_gpio.c", "general_encoder.c"):
        require((STM32 / "AllLibs/PeriphGeneral/Src" / name).resolve() in paths,
                f"Library source lost: {name}")

    includes = target.findtext("./TargetOption/TargetArmAds/Cads/VariousControls/IncludePath")
    require(includes is not None, "Keil include paths missing")
    include_paths = {(PROJECT.parent / p.replace("\\", "/")).resolve()
                     for p in includes.split(";") if p}
    for path in include_paths:
        require(path.is_dir(), f"Include directory missing: {path}")
    require((ROOT / "ZYNQ7020/gui/src").resolve() in include_paths, "Shared codec include lost")
    require((STM32 / "Core/Oscill").resolve() in include_paths, "Application include lost")
    for directory in ("AllLibs/PeriphGeneral/Inc", "AllLibs/MyLibs/MyLibs/Inc"):
        require((STM32 / directory).resolve() in include_paths, f"Library include lost: {directory}")

    output = target.findtext("./TargetOption/TargetCommonOption/OutputDirectory")
    require(output is not None, "Output directory missing")
    require((PROJECT.parent / output.replace("\\", "/")).resolve() == STM32 / "output",
            "Keil output must stay outside source directories")

    sections = user_sections((STM32 / "Core/Src/main.c").read_text(encoding="utf-8-sig"))
    require('#include "oscill_main.h"' in sections.get("Includes", ""), "Application include lost")
    require("Oscill_Init();" in sections.get("2", ""), "Application initialization lost")
    main_source = (STM32 / "Core/Src/main.c").read_text(encoding="utf-8-sig")
    require(main_source.index("MX_ADC1_Init();") < main_source.index("Oscill_Init();"),
            "ADC must be initialized before application calibration")
    require("Oscill_Process();" in sections.get("3", ""), "Application loop lost")
    require("__HAL_AFIO_REMAP_SWJ_NOJTAG();" in
            (STM32 / "Core/Src/stm32f1xx_hal_msp.c").read_text(encoding="utf-8-sig"),
            "JTAG pins required by panel were not released")
    require((STM32 / "Drivers/CMSIS/Include/core_cm3.h").is_file(), "Local CMSIS core missing")


if __name__ == "__main__":
    check_project()
    print("PASS: CubeMX configuration, application hooks, Keil sources/includes/output")
