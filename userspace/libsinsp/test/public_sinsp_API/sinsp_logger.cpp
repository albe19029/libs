// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#if defined(__linux__)
#include <libsinsp/test/helpers/scoped_file_descriptor.h>
#include <libsinsp/test/helpers/scoped_pipe.h>
#endif

#include <gtest/gtest.h>
#include <libsinsp/sinsp.h>
#include <libsinsp/logger.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace
{

/** Default size for read buffers, must be <= the size of a pipe. */
const size_t BUFFER_SIZE = 4096;

/** The default log message content. */
const std::string DEFAULT_MESSAGE = "hello, world";

class sinsp_logger_test : public testing::Test
{
public:
	sinsp_logger_test() {}

	void SetUp()
	{
		libsinsp_logger()->reset();
		s_cb_output.clear();
	}

	void TearDown() {}

protected:
#if defined(__linux__)
	/**
	 * Counts the number of non-overlapping times the given substr appears
	 * in the given target string.
	 *
	 * @param[in]  target The string to search for substrings.
	 * @param[in]  substr The substring for which to search.
	 * @param[out] count  The number of times substr was found in target.
	 */
	void count_substrings(const std::string& target, const std::string& substr, size_t& count)
	{
		size_t position = target.find(substr);

		count = 0;

		// Ensure that the string begins with the substring
		ASSERT_EQ(0, position);

		++count;

		while ((position = target.find(substr, position + 1)) != std::string::npos)
		{
			++count;
		}
	}

	/**
	 * Read the content of the given filename in to the given string.
	 *
	 * @param[in]  filename The name of the file to read.
	 * @param[out] out      The content of the file.
	 */
	void read_file(const std::string& filename, std::string& out)
	{
		test_helpers::scoped_file_descriptor fd(open(filename.c_str(), O_RDONLY));

		ASSERT_TRUE(fd.is_valid());

		nb_read_fd(fd.get_fd(), out);
	}

	/**
	 * Perform a non-blocking read on the given file descriptor and read
	 * the content (if any) from the file descriptor into the given str.
	 * Note that this function leaves the given file descriptor in non-
	 * blocking mode.
	 *
	 * @param[in]  fd  The file descriptor from which to read.
	 * @param[out] str The content read from the given fd
	 */
	void nb_read_fd(const int fd, std::string& str)
	{
		char buffer[BUFFER_SIZE] = {};

		set_nonblocking(fd);

		int res = read(fd, buffer, sizeof(buffer) - 1);
		ASSERT_GE(res, 0);

		str = buffer;
	}

	/**
	 * Write the given message with the given severity to the logger.
	 * Record and return any output generated by the logger.
	 *
	 * @param[out] std_out  Output generated by the logger to standard
	 *                      output.
	 * @param[out] std_err  Output generated by the logger to standard
	 *                      error.
	 * @param[out] file_out Output generated by the logger to the given
	 *                      log_file.
	 * @param[in]  severity The severity with which to generate the log.
	 * @param[in]  log_filename The name of the file to which the logger
	 *                          might write logs.
	 */
	void generate_log(const std::string& message,
	                  std::string& std_out,
	                  std::string& std_err,
	                  std::string& file_out,
	                  const sinsp_logger::severity severity = sinsp_logger::SEV_INFO,
	                  const std::string& log_filename = "")
	{
		test_helpers::scoped_pipe stdout_pipe;
		test_helpers::scoped_pipe stderr_pipe;

		std_out.clear();
		std_err.clear();
		file_out.clear();

		const pid_t pid = fork();

		ASSERT_TRUE(pid >= 0);

		if (pid == 0)  // child
		{
			ASSERT_TRUE(dup2(stdout_pipe.write_end().get_fd(), STDOUT_FILENO) >= 0);
			ASSERT_TRUE(dup2(stderr_pipe.write_end().get_fd(), STDERR_FILENO) >= 0);

			stdout_pipe.close();
			stderr_pipe.close();

			libsinsp_logger()->log(message, severity);

			_exit(0);
		}
		else  // parent
		{
			int status = 0;

			ASSERT_EQ(waitpid(pid, &status, 0), pid);

			ASSERT_TRUE(WIFEXITED(status));

			stdout_pipe.write_end().close();
			stderr_pipe.write_end().close();

			nb_read_fd(stdout_pipe.read_end().get_fd(), std_out);
			stdout_pipe.read_end().close();

			nb_read_fd(stderr_pipe.read_end().get_fd(), std_err);
			stderr_pipe.read_end().close();

			if (log_filename != "")
			{
				read_file(log_filename.c_str(), file_out);
			}
		}
	}
#endif

	/**
	 * This is used by some tests as the callback logging function.
	 * It records the log message to s_cb_output.
	 *
	 * @param[in] str The log message
	 * @param[in] sev The log severity
	 */
	static void log_callback_fn(std::string&& str, const sinsp_logger::severity sev)
	{
		s_cb_output = std::move(str);
	}

	/**
	 * Returns a copy of any output written to the logging callback
	 * function.
	 */
	static const std::string& get_callback_output() { return s_cb_output; }

private:
#if defined(__linux__)
	/**
	 * Put the given file descriptor in non-blocking mode.
	 *
	 * @param[in] fd The file descriptor to be placed in non-blocking mode.
	 */
	void set_nonblocking(const int fd)
	{
		int flags = fcntl(fd, F_GETFL);

		ASSERT_TRUE(flags >= 0);

		flags |= O_NONBLOCK;

		ASSERT_TRUE(fcntl(fd, F_SETFL, flags) >= 0);
	}
#endif

	static std::string s_cb_output;
};

std::string sinsp_logger_test::s_cb_output;

}  // end namespace

TEST_F(sinsp_logger_test, constructor)
{
	ASSERT_FALSE(libsinsp_logger()->has_output());
	ASSERT_EQ(libsinsp_logger()->get_severity(), sinsp_logger::SEV_INFO);
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_NONE);
}

TEST_F(sinsp_logger_test, output_type)
{
	ASSERT_FALSE(libsinsp_logger()->has_output());
	libsinsp_logger()->add_stdout_log();
	libsinsp_logger()->add_stderr_log();
	libsinsp_logger()->disable_timestamps();
	libsinsp_logger()->add_encoded_severity();
	libsinsp_logger()->add_callback_log(log_callback_fn);

	// int fd = open(".", O_WRONLY | O_TMPFILE, 0);

	int fd = open("./xyazd", O_RDWR | O_CREAT, S_IWUSR);
	libsinsp_logger()->add_file_log("./xyazd");
	close(fd);

	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), (sinsp_logger::OT_STDOUT | sinsp_logger::OT_STDERR | sinsp_logger::OT_FILE | sinsp_logger::OT_CALLBACK | sinsp_logger::OT_NOTS | sinsp_logger::OT_ENCODE_SEV));

	libsinsp_logger()->remove_callback_log();
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), (sinsp_logger::OT_STDOUT | sinsp_logger::OT_STDERR | sinsp_logger::OT_FILE | sinsp_logger::OT_NOTS | sinsp_logger::OT_ENCODE_SEV));
	ASSERT_TRUE(libsinsp_logger()->has_output());
}

TEST_F(sinsp_logger_test, get_set_severity)
{
	libsinsp_logger()->set_severity(sinsp_logger::SEV_FATAL);
	ASSERT_EQ(libsinsp_logger()->get_severity(), sinsp_logger::SEV_FATAL);
	ASSERT_TRUE(libsinsp_logger()->is_enabled(sinsp_logger::SEV_FATAL));
	ASSERT_FALSE(libsinsp_logger()->is_enabled(sinsp_logger::SEV_TRACE));
	ASSERT_FALSE(libsinsp_logger()->is_enabled(sinsp_logger::SEV_CRITICAL));
	libsinsp_logger()->set_severity(sinsp_logger::SEV_NOTICE);
	ASSERT_FALSE(libsinsp_logger()->is_enabled(sinsp_logger::SEV_INFO));
	ASSERT_TRUE(libsinsp_logger()->is_enabled(sinsp_logger::SEV_ERROR));
}

TEST_F(sinsp_logger_test, initial_state)
{
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_NONE);
	ASSERT_EQ(libsinsp_logger()->get_severity(), sinsp_logger::SEV_INFO);
}

#if defined(__linux__)
/**
 * With no enabled log sinks, calls to the logging API should produce no
 * output.
 */
TEST_F(sinsp_logger_test, log_no_output)
{
	std::string out;
	std::string err;
	std::string file;


	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_FATAL);

	ASSERT_EQ(out, "");
	ASSERT_EQ(err, "");
	ASSERT_EQ(file, "");
}

/**
 * Ensure that if the logger's severity is higher than the logged message's
 * severity, that the message is not emitted to the log sink.
 */
TEST_F(sinsp_logger_test, low_severity_not_logged)
{
	std::string out;
	std::string err;
	std::string file;

	libsinsp_logger()->set_severity(sinsp_logger::SEV_ERROR);
	ASSERT_EQ(libsinsp_logger()->get_severity(), sinsp_logger::SEV_ERROR);

	libsinsp_logger()->add_stdout_log();
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_STDOUT);

	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_INFO);

	ASSERT_EQ(out, "");
	ASSERT_EQ(err, "");
	ASSERT_EQ(file, "");
}

/**
 * With stdout logging sink enabled, emitted logs should be written only to
 * standard output.
 */
TEST_F(sinsp_logger_test, log_standard_output)
{
	std::string out;
	std::string err;
	std::string file;

	libsinsp_logger()->add_stdout_log();
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_STDOUT);

	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_FATAL);

	ASSERT_NE(out.find(DEFAULT_MESSAGE), std::string::npos);
	ASSERT_EQ(err, "");
	ASSERT_EQ(file, "");
}

/**
 * With a standard ouput logging sink enabled (and the sinsp_logger::OT_ENCODE_SEV
 * enabled), emitted logs should be written only to standard output, and those
 * logs contain the encoded severity before the timestamp
 */
TEST_F(sinsp_logger_test, log_standard_output_severity)
{
	std::string out;
	std::string err;
	std::string file;

	libsinsp_logger()->add_stdout_log();
	libsinsp_logger()->add_encoded_severity();

	ASSERT_EQ(libsinsp_logger()->get_log_output_type(),
	          (sinsp_logger::OT_STDOUT | sinsp_logger::OT_ENCODE_SEV));

	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_FATAL);

	// 8 chars for the encoded severity, 22 for the timestamp
	ASSERT_EQ(out.find(DEFAULT_MESSAGE), 8 + 22);

	sinsp_logger::severity sev;
	ASSERT_GT(sinsp_logger::decode_severity(out, sev), 0);
	ASSERT_EQ(sinsp_logger::SEV_FATAL, sev);
	ASSERT_EQ(err, "");
	ASSERT_EQ(file, "");
}

/**
 * With a standard ouput logging sink enabled (and the sinsp_logger::OT_NOTS
 * enabled), emitted logs should be written only to standard output, and those
 * logs do not contain the timestamp.
 */
TEST_F(sinsp_logger_test, log_standard_output_nots)
{
	std::string out;
	std::string err;
	std::string file;

	libsinsp_logger()->add_stdout_log();
	libsinsp_logger()->disable_timestamps();

	ASSERT_EQ(libsinsp_logger()->get_log_output_type(),
	          (sinsp_logger::OT_STDOUT | sinsp_logger::OT_NOTS));

	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_FATAL);

	// The logging API appends a newline
	const std::string expected = DEFAULT_MESSAGE + "\n";

	ASSERT_EQ(expected, out);
	ASSERT_EQ(err, "");
	ASSERT_EQ(file, "");
}

/**
 * With stderr logging sink enabled, emitted logs should be written only to
 * standard error.
 */
TEST_F(sinsp_logger_test, log_standard_error)
{
	std::string out;
	std::string err;
	std::string file;

	libsinsp_logger()->add_stderr_log();
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_STDERR);

	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_FATAL);

	ASSERT_EQ(out, "");
	ASSERT_NE(err.find(DEFAULT_MESSAGE), std::string::npos);
	ASSERT_EQ(file, "");
}

/**
 * With file logging sink enabled, emitted logs should be written only to the
 * file.
 */
TEST_F(sinsp_logger_test, log_file)
{
	const std::string filename = "/tmp/ut.out";  // FIXME
	std::string out;
	std::string err;
	std::string file;

	libsinsp_logger()->add_file_log(filename);
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_FILE);

	generate_log(DEFAULT_MESSAGE, out, err, file, sinsp_logger::SEV_FATAL, filename);

	ASSERT_EQ(out, "");
	ASSERT_EQ(err, "");
	ASSERT_NE(file.find(DEFAULT_MESSAGE), std::string::npos);
}

/**
 * With a callback logging sink enabled, emitted logs should be written only to
 * the callback.
 */
TEST_F(sinsp_logger_test, log_callback)
{
	libsinsp_logger()->add_callback_log(log_callback_fn);
	ASSERT_EQ(libsinsp_logger()->get_log_output_type(), sinsp_logger::OT_CALLBACK);

	libsinsp_logger()->log(DEFAULT_MESSAGE, sinsp_logger::SEV_FATAL);

	ASSERT_NE(get_callback_output().find(DEFAULT_MESSAGE), std::string::npos);
}

TEST_F(sinsp_logger_test, log_stderr_multithreaded)
{
	const size_t NUM_THREADS = 5;
	const std::string message = "123456789";  // 9 characters
	const size_t NUM_LOGS = 80;
	const size_t NUM_SUBSTRINGS = NUM_THREADS * NUM_LOGS;

	// 5 threads *
	// 80 logs of each 9 characters (plus a '\n') = 10 characters *
	// = 4000 characters, which should be less than BUFFER_SIZE
	ASSERT_TRUE((NUM_SUBSTRINGS * (message.size() + 1)) < (BUFFER_SIZE - 1));

	libsinsp_logger()->add_stderr_log();
	libsinsp_logger()->disable_timestamps();

	const int original_stderr = dup(STDERR_FILENO);
	ASSERT_TRUE(original_stderr >= 0);

	int pipe_fds[2] = {};
	ASSERT_EQ(0, pipe(pipe_fds));

	// Make stderr be the write end of the pipe
	ASSERT_TRUE(dup2(pipe_fds[1], STDERR_FILENO) >= 0);
	ASSERT_EQ(0, close(pipe_fds[1]));

	std::vector<std::thread> threads(NUM_THREADS);

	// Create NUM_THREADS threads, each of which will write NUM_LOGS
	// instances of the message.
	for (size_t i = 0; i < NUM_THREADS; ++i)
	{
		threads[i] = std::thread(
		    [message]()
		    {
			    for (size_t i = 0; i < NUM_LOGS; ++i)
			    {
				    const std::string new_str =
				        libsinsp_logger()->format_and_return(sinsp_logger::SEV_FATAL,
				                                       "%s",
				                                       message.c_str());

				    // Make sure that multiple threads aren't
				    // writing to the same underlying buffer
				    ASSERT_EQ(message, new_str);

				    // Normally we wouldn't want to do something
				    // like this, but hopefully this will result
				    // in more thread interleaving between the
				    // threads.
				    std::this_thread::yield();
			    }
		    });
	}

	// Wait for all the threads to finish
	for (size_t i = 0; i < NUM_THREADS; ++i)
	{
		threads[i].join();
	}

	std::string stderr_output;
	nb_read_fd(pipe_fds[0], stderr_output);

	size_t substr_count = 0;
	count_substrings(stderr_output, message, substr_count);
	ASSERT_EQ(NUM_SUBSTRINGS, substr_count);

	ASSERT_TRUE(dup2(original_stderr, STDERR_FILENO) >= 0);
	close(original_stderr);
}
#endif
