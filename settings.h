// **********************************
// * Settings                       *
// **********************************

// * Serial baud rate
#define BAUD_RATE 115200

// * Header pins
#define SDA_PIN D2
#define SDC_PIN D1

// * Define display i2c address
#define I2C_DISPLAY_ADDRESS 0x3C

// * Define time settings
#define UTC_OFFSET +1
#define NTP_SERVERS "0.nl.pool.ntp.org", "1.nl.pool.ntp.org", "2.nl.pool.ntp.org"

// * Hostname
#define HOSTNAME "weatherstation"

// * The password used for uploading
#define OTA_PASSWORD "admin"

// * Wifi timeout in milliseconds
#define WIFI_TIMEOUT 30000

// * Watchdog timer
#define OSWATCH_RESET_TIME 300

// * Watchdog: Will be updated each loop
static unsigned long last_loop;

// * Used by wifi manager to determine if settings should be saved
bool shouldSaveConfig = false;

// * Display window change interval
const int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes

// * Flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;
