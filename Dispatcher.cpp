#include "Dispatcher.hpp"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <algorithm>
#include <iterator>
#include "precomp.hpp"
#include "Tron.hpp"

using namespace std;

static void printResult(cl_ulong4 seed, cl_ulong round, result r, cl_uchar score, const std::chrono::time_point<std::chrono::steady_clock> & timeStart, const Mode & mode, std::mutex& fileMutex) {
	const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - timeStart).count();

	cl_ulong4 seedRes = seed;
	cl_ulong carry = 0;

	auto add64 = [](cl_ulong& target, cl_ulong amount) {
		cl_ulong old = target;
		target += amount;
		return (target < old) ? 1ULL : 0ULL;
	};

	seedRes.s[0] = seed.s[0] + round + 1; carry = seedRes.s[0] < (round + 1);
	seedRes.s[1] = seed.s[1] + carry; carry = seedRes.s[1] < carry;
	seedRes.s[2] = seed.s[2] + carry; carry = seedRes.s[2] < carry;
	seedRes.s[3] = seed.s[3] + carry + r.foundId;

	std::ostringstream ss;
	ss << std::hex << std::setfill('0');
	ss << std::setw(16) << seedRes.s[3] << std::setw(16) << seedRes.s[2] << std::setw(16) << seedRes.s[1] << std::setw(16) << seedRes.s[0];
	const std::string strPrivate = ss.str();

	const std::string strPublic = Tron::hexToAddress(r.foundHash);

	const std::string strVT100ClearLine = "\33[2K\r";
	std::cout << strVT100ClearLine << "  Time: " << std::setw(5) << seconds << "s Score: " << std::setw(2) << (int) score << " Private: " << strPrivate << ' ';

	std::cout << mode.transformName();
	std::cout << ": " << strPublic << std::endl;

	{
		std::lock_guard<std::mutex> lock(fileMutex);
		std::ofstream outFile("trx.txt", std::ios::app);
		if (outFile.is_open()) {
			outFile << "Time: " << seconds << "s Score: " << (int)score << " Private: " << strPrivate << " Address: " << strPublic << std::endl;
			outFile.close();
		}
	}
}

unsigned int getKernelExecutionTimeMicros(cl_event & e) {
	cl_ulong timeStart = 0, timeEnd = 0;
	clWaitForEvents(1, &e);
	clGetEventProfilingInfo(e, CL_PROFILING_COMMAND_START, sizeof(timeStart), &timeStart, NULL);
	clGetEventProfilingInfo(e, CL_PROFILING_COMMAND_END, sizeof(timeEnd), &timeEnd, NULL);
	return (timeEnd - timeStart) / 1000;
}

Dispatcher::OpenCLException::OpenCLException(const std::string s, const cl_int res) :
	std::runtime_error( s + " (res = " + toString(res) + ")"),
	m_res(res)
{

}

void Dispatcher::OpenCLException::OpenCLException::throwIfError(const std::string s, const cl_int res) {
	if (res != CL_SUCCESS) {
		throw OpenCLException(s, res);
	}
}

cl_command_queue Dispatcher::Device::createQueue(cl_context & clContext, cl_device_id & clDeviceId) {
#ifdef PROFANITY_DEBUG
	cl_command_queue_properties p = CL_QUEUE_PROFILING_ENABLE;
#else
	cl_command_queue_properties p = 0;
#endif

#ifdef CL_VERSION_2_0
	const cl_command_queue ret = clCreateCommandQueueWithProperties(clContext, clDeviceId, &p, NULL);
#else
	const cl_command_queue ret = clCreateCommandQueue(clContext, clDeviceId, p, NULL);
#endif
	return ret == NULL ? throw std::runtime_error("failed to create command queue") : ret;
}

cl_kernel Dispatcher::Device::createKernel(cl_program & clProgram, const std::string s) {
	cl_kernel ret  = clCreateKernel(clProgram, s.c_str(), NULL);
	return ret == NULL ? throw std::runtime_error("failed to create kernel \"" + s + "\"") : ret;
}

cl_ulong4 Dispatcher::Device::createSeed() {
#ifdef PROFANITY_DEBUG
	cl_ulong4 r;
	r.s[0] = 1;
	r.s[1] = 1;
	r.s[2] = 1;
	r.s[3] = 1;
	return r;
#else
	std::random_device rd;
	uint32_t seed_data[16];
	for (int i = 0; i < 16; ++i) {
		seed_data[i] = rd();
	}
	std::seed_seq ss(std::begin(seed_data), std::end(seed_data));
	std::mt19937_64 eng(ss);
	std::uniform_int_distribution<cl_ulong> distr;

	cl_ulong4 r;
	r.s[0] = distr(eng);
	r.s[1] = distr(eng);
	r.s[2] = distr(eng);
	r.s[3] = distr(eng);
	return r;
#endif
}

Dispatcher::Device::Device(Dispatcher & parent, cl_context & clContext, cl_program & clProgram, cl_device_id clDeviceId, const size_t worksizeLocal, const size_t size, const size_t index, const Mode & mode) :
	m_parent(parent),
	m_index(index),
	m_clDeviceId(clDeviceId),
	m_worksizeLocal(worksizeLocal),
	m_clScoreMax(0),
	m_clQueue(createQueue(clContext, clDeviceId) ),
	m_kernelInit( createKernel(clProgram, "profanity_init") ),
	m_kernelInverse(createKernel(clProgram, "profanity_inverse")),
	m_kernelIterate(createKernel(clProgram, "profanity_iterate")),
	m_kernelTransform( mode.transformKernel() == "" ? NULL : createKernel(clProgram, mode.transformKernel())),
	m_kernelScore(createKernel(clProgram, mode.kernel)),
	m_memPrecomp(clContext, m_clQueue, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, sizeof(g_precomp), g_precomp),
	m_memPointsDeltaX(clContext, m_clQueue, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, size, true),
	m_memInversedNegativeDoubleGy(clContext, m_clQueue, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, size, true),
	m_memPrevLambda(clContext, m_clQueue, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, size, true),
	m_memResult(clContext, m_clQueue, CL_MEM_READ_WRITE, PROFANITY_MAX_SCORE + 1),
	m_memData1(clContext, m_clQueue, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, 20),
	m_memData2(clContext, m_clQueue, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, 20),
	m_clSeed(createSeed()),
	m_round(0),
	m_speed(PROFANITY_SPEEDSAMPLES),
	m_sizeInitialized(0),
	m_eventFinished(NULL)
{

}

Dispatcher::Device::~Device() {

}

Dispatcher::Dispatcher(cl_context & clContext, cl_program & clProgram, const Mode mode, const size_t worksizeMax, const size_t inverseSize, const size_t inverseMultiple, const cl_uchar clScoreQuit)
	: m_clContext(clContext), m_clProgram(clProgram), m_mode(mode), m_worksizeMax(worksizeMax), m_inverseSize(inverseSize), m_size(inverseSize*inverseMultiple), m_clScoreMax(mode.score), m_clScoreQuit(clScoreQuit), m_eventFinished(NULL), m_countPrint(0) {

}

Dispatcher::~Dispatcher() {

}

void Dispatcher::addDevice(cl_device_id clDeviceId, const size_t worksizeLocal, const size_t index) {
	Device * pDevice = new Device(*this, m_clContext, m_clProgram, clDeviceId, worksizeLocal, m_size, index, m_mode);
	m_vDevices.push_back(pDevice);
}

void Dispatcher::run() {
	m_eventFinished = clCreateUserEvent(m_clContext, NULL);
	timeStart = std::chrono::steady_clock::now();

	if (m_mode.name == "vanity_file") {
		loadVanityFile(m_mode.vanity_filename, m_mode.prefix_len, m_mode.suffix_len);
	}

	init();

	const auto timeInitialization = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - timeStart).count();
	std::cout << "Initialization time: " << timeInitialization << " seconds" << std::endl;

	m_quit = false;
	m_countRunning = m_vDevices.size();

	std::cout << "Running..." << std::endl;
	std::cout << "  Tron Vanity Address Generator (Secured Version)" << std::endl;
	std::cout << std::endl;

	for (auto it = m_vDevices.begin(); it != m_vDevices.end(); ++it) {
		dispatch(*(*it));
	}

	clWaitForEvents(1, &m_eventFinished);
	clReleaseEvent(m_eventFinished);
	m_eventFinished = NULL;
}

void Dispatcher::init() {
	std::cout << "Initializing devices..." << std::endl;
	std::cout << "  This should take less than a minute. The number of objects initialized on each" << std::endl;
	std::cout << "  device is equal to inverse-size * inverse-multiple. To lower" << std::endl;
	std::cout << "  initialization time (and memory footprint) I suggest lowering the" << std::endl;
	std::cout << "  inverse-multiple first. You can do this via the -I switch. Do note that" << std::endl;
	std::cout << "  this might negatively impact your performance." << std::endl;
	std::cout << std::endl;

	const auto deviceCount = m_vDevices.size();
	m_sizeInitTotal = m_size * deviceCount;
	m_sizeInitDone = 0;

	cl_event * const pInitEvents = new cl_event[deviceCount];

	for (size_t i = 0; i < deviceCount; ++i) {
		pInitEvents[i] = clCreateUserEvent(m_clContext, NULL);
		m_vDevices[i]->m_eventFinished = pInitEvents[i];
		initBegin(*m_vDevices[i]);
	}

	clWaitForEvents(deviceCount, pInitEvents);
	for (size_t i = 0; i < deviceCount; ++i) {
		m_vDevices[i]->m_eventFinished = NULL;
		clReleaseEvent(pInitEvents[i]);
	}

	delete[] pInitEvents;

	std::cout << std::endl;
}

void Dispatcher::initBegin(Device & d) {
	for (auto i = 0; i < 20; ++i) {
		d.m_memData1[i] = m_mode.data1[i];
		d.m_memData2[i] = m_mode.data2[i];
	}

	d.m_memPrecomp.write(true);
	d.m_memData1.write(true);
	d.m_memData2.write(true);

	d.m_memPrecomp.setKernelArg(d.m_kernelInit, 0);
	d.m_memPointsDeltaX.setKernelArg(d.m_kernelInit, 1);
	d.m_memPrevLambda.setKernelArg(d.m_kernelInit, 2);
	d.m_memResult.setKernelArg(d.m_kernelInit, 3);
	CLMemory<cl_ulong4>::setKernelArg(d.m_kernelInit, 4, d.m_clSeed);

	d.m_memPointsDeltaX.setKernelArg(d.m_kernelInverse, 0);
	d.m_memInversedNegativeDoubleGy.setKernelArg(d.m_kernelInverse, 1);

	d.m_memPointsDeltaX.setKernelArg(d.m_kernelIterate, 0);
	d.m_memInversedNegativeDoubleGy.setKernelArg(d.m_kernelIterate, 1);
	d.m_memPrevLambda.setKernelArg(d.m_kernelIterate, 2);

	if(d.m_kernelTransform) {
		d.m_memInversedNegativeDoubleGy.setKernelArg(d.m_kernelTransform, 0);
	}

	d.m_memInversedNegativeDoubleGy.setKernelArg(d.m_kernelScore, 0);
	d.m_memResult.setKernelArg(d.m_kernelScore, 1);
	d.m_memData1.setKernelArg(d.m_kernelScore, 2);
	d.m_memData2.setKernelArg(d.m_kernelScore, 3);

	CLMemory<cl_uchar>::setKernelArg(d.m_kernelScore, 4, d.m_clScoreMax);

	initContinue(d);
}

void Dispatcher::initContinue(Device & d) {
	size_t sizeLeft = m_size - d.m_sizeInitialized;
	const size_t sizeInitLimit = m_size / 100;

	const size_t percentDone = m_sizeInitDone * 100 / m_sizeInitTotal;
	std::cout << "  " << percentDone << "%\r" << std::flush;

	if (sizeLeft) {
		cl_event event;
		const size_t sizeRun = std::min(sizeInitLimit, std::min(sizeLeft, m_worksizeMax));
		const auto resEnqueue = clEnqueueNDRangeKernel(d.m_clQueue, d.m_kernelInit, 1, &d.m_sizeInitialized, &sizeRun, NULL, 0, NULL, &event);
		OpenCLException::throwIfError("kernel queueing failed during initilization", resEnqueue);

		clFlush(d.m_clQueue);

		std::lock_guard<std::mutex> lock(m_mutex);
		d.m_sizeInitialized += sizeRun;
		m_sizeInitDone += sizeRun;

		clSetEventCallback(event, CL_COMPLETE, staticCallback, &d);
	} else {
		clSetUserEventStatus(d.m_eventFinished, CL_COMPLETE);
	}
}

void Dispatcher::dispatch(Device & d) {
	enqueueKernelDevice(d, d.m_kernelInverse, m_size / m_inverseSize, NULL);
	enqueueKernelDevice(d, d.m_kernelIterate, m_size, NULL);
	if(d.m_kernelTransform) {
		enqueueKernelDevice(d, d.m_kernelTransform, m_size, NULL);
	}
	enqueueKernelDevice(d, d.m_kernelScore, m_size, NULL);

	cl_event event;
	d.m_memResult.read(false, &event);

	clFlush(d.m_clQueue);
	clSetEventCallback(event, CL_COMPLETE, staticCallback, &d);
}

void Dispatcher::enqueueKernel(cl_command_queue & clQueue, cl_kernel & clKernel, size_t worksizeGlobal, const size_t worksizeLocal, cl_event * pEvent) {
	const size_t worksizeMax = m_worksizeMax;
	size_t worksizeOffset = 0;
	while (worksizeGlobal) {
		const size_t worksizeRun = std::min(worksizeGlobal, worksizeMax);
		const size_t * const pWorksizeLocal = (worksizeLocal == 0 ? NULL : &worksizeLocal);
		const auto res = clEnqueueNDRangeKernel(clQueue, clKernel, 1, &worksizeOffset, &worksizeRun, pWorksizeLocal, 0, NULL, pEvent);
		OpenCLException::throwIfError("kernel queueing failed", res);

		worksizeGlobal -= worksizeRun;
		worksizeOffset += worksizeRun;
	}
}

void Dispatcher::enqueueKernelDevice(Device & d, cl_kernel & clKernel, size_t worksizeGlobal, cl_event * pEvent) {
	try {
		enqueueKernel(d.m_clQueue, clKernel, worksizeGlobal, d.m_worksizeLocal, pEvent);
	} catch ( OpenCLException & e ) {
		if ((e.m_res == CL_INVALID_WORK_GROUP_SIZE || e.m_res == CL_INVALID_WORK_ITEM_SIZE) && d.m_worksizeLocal != 0) {
			std::cout << std::endl << "warning: local work size abandoned on GPU" << d.m_index << std::endl;
			d.m_worksizeLocal = 0;
			enqueueKernel(d.m_clQueue, clKernel, worksizeGlobal, d.m_worksizeLocal, pEvent);
		}
		else {
			throw;
		}
	}
}

void Dispatcher::handleResult(Device & d) {
	bool bFound = false;

	for (cl_uchar i = PROFANITY_MAX_SCORE; i > 0; --i) {
		if (d.m_memResult[i].found > 0) {
			if (i >= m_clScoreMax) {
				std::lock_guard<std::mutex> lock(m_mutex);

				bool shouldPrint = true;
				if (m_mode.name == "vanity_file") {
					std::string address = Tron::hexToAddress(d.m_memResult[i].foundHash);
					shouldPrint = false;

					for (size_t targetIdx = 0; targetIdx < m_target_prefixes.size(); ++targetIdx) {
						bool prefixMatch = true;
						if (m_mode.prefix_len > 0) {
							prefixMatch = address.substr(0, m_mode.prefix_len) == m_target_prefixes[targetIdx];
						} else if (m_mode.suffix_len == 0) {
							prefixMatch = (address == m_target_prefixes[targetIdx]);
						}

						bool suffixMatch = true;
						if (m_mode.suffix_len > 0) {
							suffixMatch = address.substr(address.length() - m_mode.suffix_len) == m_target_suffixes[targetIdx];
						}

						if (prefixMatch && suffixMatch) {
							shouldPrint = true;
							break;
						}
					}
				}

				if (shouldPrint) {
					printResult(d.m_clSeed, d.m_round, d.m_memResult[i], i, timeStart, m_mode, m_file_mutex);

					if (m_clScoreQuit > 0 && i >= m_clScoreQuit) {
						m_quit = true;
					}

					if (i > m_clScoreMax) {
						m_clScoreMax = i;
						for (auto & dev : m_vDevices) {
							dev->m_clScoreMax = m_clScoreMax;
							CLMemory<cl_uchar>::setKernelArg(dev->m_kernelScore, 4, m_clScoreMax);
						}
					}
				}
			}

			d.m_memResult[i].found = 0;
			bFound = true;
		}
	}

	if (bFound) {
		d.m_memResult.write(false);
	}
}

void Dispatcher::loadVanityFile(const std::string filename, int t, int w) {
	std::ifstream file(filename);
	if (!file.is_open()) {
		throw std::runtime_error("Could not open vanity file: " + filename);
	}

	std::string line;
	while (std::getline(file, line)) {
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		if (line.empty()) continue;

		if (line[0] != 'T' || line.length() < 34) {
			continue;
		}

		if (t > 0) {
			m_target_prefixes.push_back(line.substr(0, t));
		} else if (w == 0) {
			m_target_prefixes.push_back(line);
		} else {
			m_target_prefixes.push_back("");
		}

		if (w > 0) {
			m_target_suffixes.push_back(line.substr(line.length() - w));
		} else {
			m_target_suffixes.push_back("");
		}
	}

	if (m_target_prefixes.empty()) {
		throw std::runtime_error("No valid TRX addresses found in vanity file.");
	}

	std::cout << "Loaded " << m_target_prefixes.size() << " target patterns from " << filename << std::endl;
}

void Dispatcher::onEvent(cl_event event, cl_int status, Device & d) {
	if (status != CL_COMPLETE) {
		std::cout << "Dispatcher::onEvent - Got bad status: " << status << std::endl;
	}
	else if (d.m_eventFinished != NULL) {
		initContinue(d);
	} else {
		++d.m_round;
		handleResult(d);

		bool bDispatch = true;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			d.m_speed.sample(m_size);
			printSpeed();

			if( m_quit ) {
				bDispatch = false;
				if(--m_countRunning == 0) {
					clSetUserEventStatus(m_eventFinished, CL_COMPLETE);
				}
			}
		}

		if (bDispatch) {
			dispatch(d);
		}
	}
}

void Dispatcher::printSpeed() {
	++m_countPrint;
	if( m_countPrint > m_vDevices.size() ) {
		std::string strGPUs;
		double speedTotal = 0;
		for (auto & e : m_vDevices) {
			const auto curSpeed = e->m_speed.getSpeed();
			speedTotal += curSpeed;
			strGPUs += " GPU" + toString(e->m_index) + ": " + formatSpeed(curSpeed);
		}

		const std::string strVT100ClearLine = "\33[2K\r";
		std::cerr << strVT100ClearLine << "Total: " << formatSpeed(speedTotal) << " -" << strGPUs << '\r' << std::flush;
		m_countPrint = 0;
	}
}

void CL_CALLBACK Dispatcher::staticCallback(cl_event event, cl_int event_command_exec_status, void * user_data) {
	Device * const pDevice = static_cast<Device *>(user_data);
	pDevice->m_parent.onEvent(event, event_command_exec_status, *pDevice);
	clReleaseEvent(event);
}

std::string Dispatcher::formatSpeed(double f) {
	const std::string S = " KMGT";

	unsigned int index = 0;
	while (f > 1000.0f && index < S.size()) {
		f /= 1000.0f;
		++index;
	}

	std::ostringstream ss;
	ss << std::fixed << std::setprecision(3) << (double)f << " " << S[index] << "H/s";
	return ss.str();
}
