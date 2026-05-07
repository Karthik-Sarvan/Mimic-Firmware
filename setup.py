from setuptools import setup, find_packages

setup(
    name="mimic-fw",
    version="1.1.0",
    packages=find_packages(),
    install_requires=[
        "pyserial>=3.5",
    ],
    entry_points={
        'console_scripts': [
            'mimic=mimic.cli:main',
        ],
    },
    author="Antigravity",
    description="Cross-platform Python library for the Mimic STM32 Firmware",
    python_requires='>=3.6',
)
