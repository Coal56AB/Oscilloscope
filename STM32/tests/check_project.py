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
        "RCC.PLLSourceVirtual": "RCC_PLLSOURCE_HSI_DIV2",
        "RCC.PLLMUL": "RCC_PLL_MUL16",
        "RCC.APB1CLKDivider": "RCC_HCLK_DIV2",
    }
    for key, value in expected.items():
        require(config.get(key) == value, f"CubeMX: {key} must be {value}")
    require(config.get("NVIC.USART1_IRQn", "").startswith("true"), "USART1 IRQ disabled")

    target = ET.parse(PROJECT).find("./Targets/Target")
    require(target is not None, "Keil target missing")
    require(target.findtext("uAC6") == "1", "Arm Compiler 6 selection lost")
    require(target.findtext("./TargetOption/TargetCommonOption/Device") == "STM32F103RC",
            "Keil target device changed")

    paths = set()
    for item in target.findall("./Groups/Group/Files/File/FilePath"):
        resolved = (PROJECT.parent / item.text.replace("\\", "/")).resolve()
        require(resolved.is_file(), f"Keil source missing: {item.text}")
        paths.add(resolved)

    for name in ("controls.c", "board_controls.c", "controls_uart.c", "oscill_main.c"):
        require((STM32 / "Core/Src" / name).resolve() in paths, f"Application source lost: {name}")
    require((ROOT / "ZYNQ7020/gui/src/control_protocol.c").resolve() in paths,
            "Shared codec source lost")

    includes = target.findtext("./TargetOption/TargetArmAds/Cads/VariousControls/IncludePath")
    require(includes is not None, "Keil include paths missing")
    include_paths = {(PROJECT.parent / p.replace("\\", "/")).resolve()
                     for p in includes.split(";") if p}
    for path in include_paths:
        require(path.is_dir(), f"Include directory missing: {path}")
    require((ROOT / "ZYNQ7020/gui/src").resolve() in include_paths, "Shared codec include lost")

    output = target.findtext("./TargetOption/TargetCommonOption/OutputDirectory")
    require(output is not None, "Output directory missing")
    require((PROJECT.parent / output.replace("\\", "/")).resolve() == STM32 / "output",
            "Keil output must stay outside source directories")

    sections = user_sections((STM32 / "Core/Src/main.c").read_text(encoding="utf-8-sig"))
    require('#include "oscill_main.h"' in sections.get("Includes", ""), "Application include lost")
    require("Oscill_Init();" in sections.get("2", ""), "Application initialization lost")
    require("Oscill_Process();" in sections.get("3", ""), "Application loop lost")
    require("__HAL_AFIO_REMAP_SWJ_NOJTAG();" in
            (STM32 / "Core/Src/stm32f1xx_hal_msp.c").read_text(encoding="utf-8-sig"),
            "JTAG pins required by panel were not released")
    require((STM32 / "Drivers/CMSIS/Include/core_cm3.h").is_file(), "Local CMSIS core missing")


if __name__ == "__main__":
    check_project()
    print("PASS: CubeMX configuration, application hooks, Keil sources/includes/output")
