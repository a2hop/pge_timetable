# PGE Timetable Extension

A PostgreSQL extension for generating time hierarchies.

## Overview

This extension provides functionality to work with time hierarchies in PostgreSQL databases, making it easier to handle date/time dimensions for analytical queries.

## Prerequisites

To build and install this extension, you'll need:

* PostgreSQL server (version 10 or later recommended)
* PostgreSQL development packages (`postgresql-server-dev-all` on Debian/Ubuntu, `postgresql-devel` on RHEL/CentOS)
* C compiler (GCC or Clang)
* Make

### Ubuntu/Debian
```bash
sudo apt-get install postgresql-server-dev-all build-essential
```

### RHEL/CentOS/Fedora
```bash
sudo yum install postgresql-devel gcc make
```

### macOS (with Homebrew)
```bash
brew install postgresql
```

## Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/a2hop/pge_timetable.git
cd pge_timetable

# Build and install
make
make install
```

### From Binary Release

1. Download the latest release from the [Releases page](https://github.com/a2hop/pge_timetable/releases)
2. Extract the files to your PostgreSQL extension directory
3. Run `CREATE EXTENSION pge_timetable;` in your database

## Usage

After installing the extension, enable it in your database:

```sql
CREATE EXTENSION pge_timetable;
```

## Features

* Time hierarchy generation
* Date dimension support
* [Add more features here]

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
