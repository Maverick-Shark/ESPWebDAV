// Arduino.h must be included first so that constrain() is always defined
// before parser.h uses it. Without this, compilation units that include
// parser.h (or macros.h) before any ESP8266WiFi/Arduino header would fail
// with "constrain was not declared in this scope".
#include <Arduino.h>

typedef uint32_t millis_t;

#define WITHIN(V,L,H) ((V) >= (L) && (V) <= (H))
#define NUMERIC(a) WITHIN(a, '0', '9')
#define DECIMAL(a) (NUMERIC(a) || a == '.')
#define NUMERIC_SIGNED(a) (NUMERIC(a) || (a) == '-' || (a) == '+')
#define DECIMAL_SIGNED(a) (DECIMAL(a) || (a) == '-' || (a) == '+')

// constrain() is provided by Arduino.h — do not redefine it here.

#define FORCE_INLINE __attribute__((always_inline)) inline
