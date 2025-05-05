MODULE_big = pge_timetable
OBJS = pge_timetable.o
EXTENSION = pge_timetable
DATA = pge_timetable--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
