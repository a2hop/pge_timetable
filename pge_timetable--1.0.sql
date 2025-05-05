-- Timetable extension for PostgreSQL - C implementation wrapper

-- Create a type to represent the structure of the monthly timetable
CREATE TYPE timetable_entry AS (
    uid int,
    y int,
    q int,
    m int,
    days int,
    ord int
);

-- Create a type to represent the structure of the daily timetable
CREATE TYPE daily_timetable_entry AS (
    uid int,
    date date,
    y int,
    q int,
    m int,
    d int,
    w1 int,          -- Calendar week (1-53), partial week at year start is week 1
    dow int,          -- Numeric day of week (1=Monday, 7=Sunday)
    doy int,          -- Day of year (1-366)
    is_weekend boolean,
    ord int
);

-- Create the function for generating monthly timetable
CREATE FUNCTION pge_get_timetable(
    start_year int DEFAULT extract(year from current_date)::int,
    end_year int DEFAULT (extract(year from current_date) + 3)::int
) RETURNS SETOF timetable_entry
AS 'MODULE_PATHNAME', 'generate_timetable_c'
LANGUAGE C STRICT;

-- Create the function for generating daily timetable
CREATE FUNCTION pge_get_daily_timetable(
    start_date date DEFAULT current_date - 100,
    end_date date DEFAULT current_date + 100
) RETURNS SETOF daily_timetable_entry
AS 'MODULE_PATHNAME', 'generate_daily_timetable_c'
LANGUAGE C;


