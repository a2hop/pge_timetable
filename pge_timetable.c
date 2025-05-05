#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/datetime.h"
#include "miscadmin.h"
#include "utils/timestamp.h"
#include "pgtime.h"

PG_MODULE_MAGIC;

/* Define the timetable entry structure */
typedef struct {
    int32 uid;
    int32 y;
    int32 q;
    int32 m;
    int32 days;
    int32 ord;
} TimetableEntry;

/* State for the SRF (Set-Returning-Function) */
typedef struct {
    int32 start_year;
    int32 end_year;
    int32 current_year;
    int32 current_month;
    int32 ord_counter;
    int32 total_months;      /* Pre-computed total months to process */
    int32 months_processed;  /* Counter for processed months */
    TimetableEntry current_entry;
    bool done;
} TimetableState;

/* Define the daily timetable entry structure */
typedef struct {
    int32 uid;
    DateADT date;
    int32 y;
    int32 q;
    int32 m;
    int32 d;
    int32 w1;         /* Calendar week (1-53) */
    int32 dow;        /* day of week numeric (1=Monday, 7=Sunday) */
    int32 doy;        /* day of year (1-366) */
    bool is_weekend;
    int32 ord;
} DailyTimetableEntry;

/* State for the daily SRF */
typedef struct {
    DateADT start_date;
    DateADT end_date;
    DateADT current_date;
    int32 ord_counter;
    int32 total_days;       /* Pre-computed total days to process */
    int32 days_processed;   /* Counter for processed days */
    DailyTimetableEntry current_entry;
    bool done;
} DailyTimetableState;

/* Look-up tables to avoid repeated calculations */
static const int days_per_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
/* Quarter lookup by month (0-based array) */
static const int month_to_quarter[] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4};

/* Optimized days in a month - avoid function call overhead for non-February months */
static inline int days_in_month(int year, int month) {
    if (month != 2)
        return days_per_month[month];
        
    /* February leap year check */
    return ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : 28;
}

/* Pre-compute day of year offsets to avoid loops during calculation */
static const int month_day_offsets[13] = {
    0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

/* Optimized day of year calculation */
static inline int get_day_of_year(int year, int month, int day) {
    int doy = month_day_offsets[month] + day;
    
    /* Adjust for leap year after February */
    if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        doy++;
        
    return doy;
}

/* Optimized calendar week calculation - uses pre-calculated day of year */
static inline int get_calendar_week(int year, int day_of_year, int jan1_wday) {
    /* Convert 0=Sunday to 0=Monday for calculation */
    jan1_wday = (jan1_wday == 0) ? 6 : (jan1_wday - 1);
    
    /* Calculate week: Add offset to account for days before first Monday,
       then integer divide by 7 and add 1 (since weeks start at 1) */
    return ((day_of_year + jan1_wday - 1) / 7) + 1;
}

/* Generate the timetable entries - optimized version */
PG_FUNCTION_INFO_V1(generate_timetable_c);
Datum
generate_timetable_c(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    TimetableState *state;
    
    /* First-call initialization */
    if (SRF_IS_FIRSTCALL()) {
        MemoryContext oldcontext;
        TupleDesc tupdesc;
        int32 start_year, end_year;
        
        /* Create function context */
        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
        
        /* Parse arguments */
        start_year = PG_GETARG_INT32(0);
        end_year = PG_GETARG_INT32(1);
        
        /* Create state structure */
        state = (TimetableState *) palloc0(sizeof(TimetableState));
        
        /* Initialize state values with pre-calculations */
        state->start_year = start_year;
        state->end_year = end_year;
        state->current_year = start_year;
        state->current_month = 1;
        state->ord_counter = 1;
        state->done = false;
        
        /* Pre-compute total months to process - optimization to avoid checks in loop */
        state->total_months = ((end_year - start_year + 1) * 12);
        state->months_processed = 0;
        
        /* Build a tuple descriptor */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept type record")));
                     
        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        funcctx->user_fctx = state;
        
        MemoryContextSwitchTo(oldcontext);
    }
    
    /* Get state */
    funcctx = SRF_PERCALL_SETUP();
    state = (TimetableState *) funcctx->user_fctx;
    
    /* Using pre-computed count instead of year/month boundary checks */
    if (state->months_processed >= state->total_months)
        SRF_RETURN_DONE(funcctx);
    
    /* Calculate quarter from month (1-based indexing) */
    state->current_entry.q = month_to_quarter[state->current_month];
    
    /* Calculate uid based on months since 1950 */
    state->current_entry.uid = 20000 + ((state->current_year - 1950) * 12) + (state->current_month - 1);
    
    state->current_entry.y = state->current_year;
    state->current_entry.m = state->current_month;
    state->current_entry.days = days_in_month(state->current_year, state->current_month);
    state->current_entry.ord = state->ord_counter++;
    
    /* Advance to next month or move to next year */
    if (++state->current_month > 12) {
        state->current_month = 1;
        state->current_year++;
    }
    
    /* Increment processed count */
    state->months_processed++;
    
    /* Create result tuple without redundant variable declarations */
    {
        Datum values[6];
        bool nulls[6] = {false};
        HeapTuple tuple;
        
        values[0] = Int32GetDatum(state->current_entry.uid);
        values[1] = Int32GetDatum(state->current_entry.y);
        values[2] = Int32GetDatum(state->current_entry.q);
        values[3] = Int32GetDatum(state->current_entry.m);
        values[4] = Int32GetDatum(state->current_entry.days);
        values[5] = Int32GetDatum(state->current_entry.ord);
        
        /* Build and return the tuple */
        tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
        
        SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
    }
}

/* Get current date as DateADT - inlined for performance */
static inline DateADT get_current_date(void)
{
    TimestampTz now = GetCurrentTimestamp();
    struct pg_tm tm;
    int tz;
    fsec_t fsec;
    
    timestamp2tm(now, &tz, &tm, &fsec, NULL, NULL);
    
    return date2j(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) - POSTGRES_EPOCH_JDATE;
}

/* Cache to store Jan 1 weekdays for years - prevents repeated calculations */
typedef struct {
    int year;
    int jan1_wday;
} YearWdayCache;

#define WDAY_CACHE_SIZE 10
static YearWdayCache wday_cache[WDAY_CACHE_SIZE]; 
static int cache_entries = 0;

/* Get cached or calculate Jan 1 weekday for a given year */
static inline int get_cached_jan1_wday(int year) {
    int i;
    int jan1_wday;
    
    /* Check cache first */
    for (i = 0; i < cache_entries; i++) {
        if (wday_cache[i].year == year)
            return wday_cache[i].jan1_wday;
    }
    
    /* Not found, calculate and cache it */
    jan1_wday = j2day(date2j(year, 1, 1));
    
    /* Only cache if we have space */
    if (cache_entries < WDAY_CACHE_SIZE) {
        wday_cache[cache_entries].year = year;
        wday_cache[cache_entries].jan1_wday = jan1_wday;
        cache_entries++;
    }
    
    return jan1_wday;
}

/* Generate daily timetable entries - optimized version */
PG_FUNCTION_INFO_V1(generate_daily_timetable_c);
Datum
generate_daily_timetable_c(PG_FUNCTION_ARGS)
{
    FuncCallContext *funcctx;
    DailyTimetableState *state;
    int year, month, day;
    int dow;
    int iso_dow;
    int day_of_year;
    int jan1_wday;
    
    /* First-call initialization */
    if (SRF_IS_FIRSTCALL()) {
        MemoryContext oldcontext;
        TupleDesc tupdesc;
        DateADT today, start_date, end_date;
        
        /* Create function context */
        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
        
        /* Create state structure */
        state = (DailyTimetableState *) palloc0(sizeof(DailyTimetableState));
        
        /* Get current date */
        today = get_current_date();
        
        /* Parse arguments or use defaults */
        start_date = PG_ARGISNULL(0) ? (today - 100) : PG_GETARG_DATEADT(0);
        end_date = PG_ARGISNULL(1) ? (today + 100) : PG_GETARG_DATEADT(1);
        
        /* Initialize state values with pre-calculations */
        state->start_date = start_date;
        state->end_date = end_date;
        state->current_date = start_date;
        state->ord_counter = 1;
        
        /* Pre-compute total days - optimization to avoid date comparison in loop */
        state->total_days = end_date - start_date + 1;
        state->days_processed = 0;
        
        /* Initialize the weekday cache */
        cache_entries = 0;
        
        /* Build a tuple descriptor */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept type record")));
                     
        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        funcctx->user_fctx = state;
        
        MemoryContextSwitchTo(oldcontext);
    }
    
    /* Get state */
    funcctx = SRF_PERCALL_SETUP();
    state = (DailyTimetableState *) funcctx->user_fctx;
    
    /* Check if we're done using pre-computed total days */
    if (state->days_processed >= state->total_days)
        SRF_RETURN_DONE(funcctx);
    
    /* Extract date parts */
    j2date(state->current_date + POSTGRES_EPOCH_JDATE, &year, &month, &day);
    
    /* Get day of week (0=Sunday, 6=Saturday) */
    dow = j2day(state->current_date + POSTGRES_EPOCH_JDATE);
    
    /* Calculate ISO day of week (1=Monday, 7=Sunday) */
    iso_dow = (dow == 0) ? 7 : dow;
    
    /* Calculate day of year (1-366) - using optimized function */
    day_of_year = get_day_of_year(year, month, day);
    
    /* Get cached Jan 1 weekday for this year */
    jan1_wday = get_cached_jan1_wday(year);
    
    /* Fill in the entry data */
    state->current_entry.uid = 2000000 + (state->current_date - date2j(1950, 1, 1) + POSTGRES_EPOCH_JDATE);
    state->current_entry.date = state->current_date;
    state->current_entry.y = year;
    state->current_entry.q = month_to_quarter[month]; /* Use lookup table instead of calculation */
    state->current_entry.m = month;
    state->current_entry.d = day;
    state->current_entry.w1 = get_calendar_week(year, day_of_year, jan1_wday);
    state->current_entry.dow = iso_dow;
    state->current_entry.doy = day_of_year;
    state->current_entry.is_weekend = (dow == 0 || dow == 6);
    state->current_entry.ord = state->ord_counter++;
    
    /* Advance to next day and count */
    state->current_date++;
    state->days_processed++;
    
    /* Create result tuple with stack-based data */
    {
        Datum values[11];
        bool nulls[11] = {false};
        HeapTuple tuple;
        
        values[0] = Int32GetDatum(state->current_entry.uid);
        values[1] = DateADTGetDatum(state->current_entry.date);
        values[2] = Int32GetDatum(state->current_entry.y);
        values[3] = Int32GetDatum(state->current_entry.q);
        values[4] = Int32GetDatum(state->current_entry.m);
        values[5] = Int32GetDatum(state->current_entry.d);
        values[6] = Int32GetDatum(state->current_entry.w1);
        values[7] = Int32GetDatum(state->current_entry.dow);
        values[8] = Int32GetDatum(state->current_entry.doy);
        values[9] = BoolGetDatum(state->current_entry.is_weekend);
        values[10] = Int32GetDatum(state->current_entry.ord);
        
        /* Build and return the tuple */
        tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
        
        SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(tuple));
    }
}
