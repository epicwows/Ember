#🔥 **Ember Log**
---

# Basic overview
*Note: For brevity, `el` is an alias of the namespace `ember::log`. This alias will be used in all code examples.*

Ember uses its own lightweight, high-performance logging library built to meet the exact needs of the project. Existing libraries were evaluated but they either carried too much baggage and complexity, had performance short-comings or lacked a desired feature.

Logging in Ember is primarily intended to help server operators assess the health of their server, as well as providing a basic form of debugging for developers. It is *not* intended to be used to generate logs required for administrative tasks such as checking login history, player trades, chat logs and character recovery; such information should be stored in a database with transactional capabilities.

The logger consists of two main components that the user must deal with: the logger object and sinks.

# Logger Creation
To create a logger, simply do the following:
```cpp
#include <logger/Logging.h>

auto logger = std::make_unique<el::Logger>();
```

That's all there is to it.

# Sinks
In order to actually send anything to the logger, sinks must be created and assigned to the logger object. Sinks simply provide an output for the logging messages.

Ember provides three sinks; console, file and remote (syslog). Each sink requires different arguments in order to construct it.

### Console Sink
To create a console sink, simply do:
```cpp
#include <logger/ConsoleSink.h>

auto sink = std::make_unique<el::ConsoleSink>(el::SEVERITY::INFO);
```

Other than setting the severity, the console sink offers no options.

### File Sink
The file sink requires three arguments; severity, filename to write the logs to and a mode that indicates, in cases where the file already exists, whether to create a new file for logging or to append to an existing file. If instructed to create a new file, it will rename the existing file, not overwrite it.

Creation example:
```cpp
#include <logger/FileSink.h>

auto sink = std::make_unique<el::FileSink>(el::SEVERITY::DEBUG, "my_log.log", el::FileSink::MODE::APPEND);
```

Filenames may include formatters as specified by C++11's put_time function. See http://en.cppreference.com/w/cpp/io/manip/put_time.

After creation, the sink may be further configured:
```cpp
sink->size_limit(5); // maximum allowed log size in MB - log will be rotated before hitting this limit
sink->log_severity(true); // whether to write the log message severity to the file
sink->log_date(true); // whether to write the timestamp to the file
sink->time_format("%Y"); // format used for writing timestamps - uses C++11's put_time formatters
sink->midnight_rotate(true); // whether to rotate the file at midnight
```

### Remote Sink
The remote sink implements the UDP-based syslog protocol to allow log messages to be sent to remote machines. This could be useful when used in conjunction with third-party remote log viewers; for example, setting up email/text-message notifications if an error is encountered.

The remote sink requires five arguments; severity, the host to send the logs to, the remote port, facility and service name. 

In Ember, the facility is always set to LOCAL_USE_0 (although it can be changed) and the service name is usually the name of the application sending the log messages. To learn more, see: http://en.wikipedia.org/wiki/Syslog

Creation example:
```cpp
#include <logger/SyslogSink.h>

auto sink = std::make_unique<el::SyslogSink>(el::SEVERITY::ERROR, "localhost", 514, el::SyslogSink::FACILITY::LOCAL_USE_0, "login");
```

# Registering Sinks
After creating the desired sinks, they must be registered with the logger. This requires giving the logger object ownership of the unique_ptr holding the sink.

For example:
```cpp
auto sink = std::make_unique<el::ConsoleSink>(el::SEVERITY::INFO);
auto logger = std::make_unique<el::Logger>();
logger->add_sink(std::move(sink));
```

# Outputting Messages
Now that we've created a logger and provided it with sinks, we can use it for logging. To log a message, simply follow the form:
```cpp
LOG_DEBUG(logger) << "Hello, world!" << LOG_ASYNC; // async log entry
LOG_INFO(logger) << "This is a blocking entry!" << LOG_SYNC; // blocking log entry
```

### LOG_SYNC vs LOG_ASYNC
LOG_ASYNC is used to provided maximum performance. It will always return immediately, without causing the logging thread to wait for the entry to be written to output by the sinks. In contrast, LOG_SYNC will block the logging thread's execution until the message has been written to output by the sinks. This could be useful when attempting to insert log entries for crash debugging (more on this later).

# Global Logging
Although it is not recommended for normal development, it may be useful to perform logging in a section of the code that doesn't have access to a logger object. For those situations, you should set a logger object as the global logger:
```cpp
el::set_global_logger(logger.get()); // where logger is a unique_ptr
```

You can then use the LOG_SEVERITY_GLOB macros to perform logging:
```cpp
#include <logger/Logging.h>

LOG_WARN_GLOB << "Hello, world! This is the global logger!" << LOG_ASYNC;
```

# Performance Notes
* When the message queue hits a certain size, the logger will begin writing log entries in batches rather than individually. This does not apply to the remote sink.
* File entries are written in binary mode rather than ASCII. This means that newline characters will not be converted to \r\n on Windows but this should not pose a problem for most text viewers.
* The logger can only log as fast as its slowest sink, with the slowest sink being console output and the fastest being the file sink. This shouldn't be an issue unless you're doing a copious amount of logging (e.g. writing every packet on a busy realm, for some reason).
* The LOG_SEVERITY macros will prevent any evaluation taking place if none of the registered sinks are interested in receiving messages of the specified severity. This means it's okay to have expensive function calls inside logging statements as they won't be evaluated unless needed. The overhead of a log message that doesn't need to be sent to any sinks is a single integer comparison (regardless of number of sinks).
* Formatting timestamps for file entries will significantly decrease throughput.
* For maximum performance, use only a file sink with timestamp and severity logging disabled. In testing, this allowed logging at a rate that appeared to be limited by the throughput of the SSD being used to save the entries to.

# Limitations & Future Work
* If the application crashes and there are LOG_ASYNC entries that are yet to be written by the sinks, they may be lost. This should be possible to solve after crash handlers have been added to Ember.
* Memory exhaustion is possible, albeit highly unlikely, when using LOG_ASYNC if the logger is unable to keep up with the rate of message receival.
* Log entries from different threads may not retain their original ordering. That is, for example, if thread #1 logs "Hello" and thread #2 logs ", world!" shortly after, the logger could output the entries as ", world!" and "Hello". Log entries will retain their original ordering on a per-thread basis.
* For performance purposes, timestamps are generated at the time of output, not when the message is sent to the logger. This is unlikely to be a problem.