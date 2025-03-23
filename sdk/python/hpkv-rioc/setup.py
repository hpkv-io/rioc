from setuptools import setup, find_packages

setup(
    name="hpkv-rioc",
    version="0.1.0",
    description="Python SDK for HPKV RIOC - High Performance Key-Value Store",
    author="HPKV Team",
    author_email="",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    package_data={
        "hpkv_rioc": [
            "runtimes/win-x64/native/rioc.dll",
            "runtimes/linux-x64/native/librioc.so",
            "runtimes/osx-x64/native/librioc.dylib",
            "runtimes/osx-arm64/native/librioc.dylib",
        ]
    },
    python_requires=">=3.8",
    install_requires=[],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: Database",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
) 