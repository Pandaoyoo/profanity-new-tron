#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <map>
#include <set>

#if defined(__APPLE__) || defined(__MACOSX)
#include <OpenCL/cl.h>
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl.h>
#include <CL/cl_ext.h>
#endif

#define CL_DEVICE_PCI_BUS_ID_NV  0x4008
#define CL_DEVICE_PCI_SLOT_ID_NV 0x4009

#include "Dispatcher.hpp"
#include "ArgParser.hpp"
#include "Mode.hpp"
#include "help.hpp"
#include "embedded_kernels.hpp"

std::string readFile(const char * const szFilename)
{
	std::ifstream in(szFilename, std::ios::in | std::ios::binary);
	if (!in.is_open()) {
		throw std::runtime_error(std::string("Could not open file: ") + szFilename);
	}
	std::ostringstream contents;
	contents << in.rdbuf();
	return contents.str();
}

std::string getOpenCLSource()
{
	try {
		return readFile("keccak.cl") + readFile("tron.cl");
	} catch (...) {
		return std::string(embedded_kernels::keccak_cl) + embedded_kernels::tron_cl;
	}
}

std::vector<cl_device_id> getAllDevices(cl_device_type deviceType = CL_DEVICE_TYPE_GPU)
{
	std::vector<cl_device_id> vDevices;

	cl_uint platformIdCount = 0;
	clGetPlatformIDs (0, NULL, &platformIdCount);

	std::vector<cl_platform_id> platformIds (platformIdCount);
	clGetPlatformIDs (platformIdCount, platformIds.data (), NULL);

	for( auto it = platformIds.cbegin(); it != platformIds.cend(); ++it ) {
		cl_uint countDevice;
		cl_int res = clGetDeviceIDs(*it, deviceType, 0, NULL, &countDevice);
		if (res != CL_SUCCESS || countDevice == 0) continue;

		std::vector<cl_device_id> deviceIds(countDevice);
		clGetDeviceIDs(*it, deviceType, countDevice, deviceIds.data(), &countDevice);

		if (vDevices.empty()) {
			std::copy( deviceIds.begin(), deviceIds.end(), std::back_inserter(vDevices) );
			break;
		}
	}

	return vDevices;
}

template <typename T, typename U, typename V, typename W>
T clGetWrapper(U function, V param, W param2) {
	T t;
	function(param, param2, sizeof(t), &t, NULL);
	return t;
}

template <typename U, typename V, typename W>
std::string clGetWrapperString(U function, V param, W param2) {
	size_t len;
	function(param, param2, 0, NULL, &len);
	char * const szString = new char[len];
	function(param, param2, len, szString, NULL);
	std::string r(szString);
	delete[] szString;
	return r;
}

template <typename U, typename V, typename W, typename X>
std::string clGetWrapperString(U function, V param, W param2, X param3) {
	size_t len;
	function(param, param2, param3, 0, NULL, &len);
	char * const szString = new char[len];
	function(param, param2, param3, len, szString, NULL);
	std::string r(szString);
	delete[] szString;
	return r;
}

template <typename T, typename U, typename V, typename W>
std::vector<T> clGetWrapperVector(U function, V param, W param2) {
	size_t len;
	function(param, param2, 0, NULL, &len);
	len /= sizeof(T);
	std::vector<T> v;
	if (len > 0) {
		T * pArray = new T[len];
		function(param, param2, len * sizeof(T), pArray, NULL);
		for (size_t i = 0; i < len; ++i) {
			v.push_back(pArray[i]);
		}
		delete[] pArray;
	}
	return v;
}

std::vector<std::string> getBinaries(cl_program & clProgram) {
	std::vector<std::string> vReturn;
	auto vSizes = clGetWrapperVector<size_t>(clGetProgramInfo, clProgram, CL_PROGRAM_BINARY_SIZES);
	if (!vSizes.empty()) {
		unsigned char * * pBuffers = new unsigned char *[vSizes.size()];
		for (size_t i = 0; i < vSizes.size(); ++i) {
			pBuffers[i] = new unsigned char[vSizes[i]];
		}

		clGetProgramInfo(clProgram, CL_PROGRAM_BINARIES, vSizes.size() * sizeof(unsigned char *), pBuffers, NULL);
		for (size_t i = 0; i < vSizes.size(); ++i) {
			std::string strData(reinterpret_cast<char *>(pBuffers[i]), vSizes[i]);
			vReturn.push_back(strData);
			delete[] pBuffers[i];
		}

		delete[] pBuffers;
	}

	return vReturn;
}

unsigned int getUniqueDeviceIdentifier(const cl_device_id & deviceId) {
	cl_int bus_id = clGetWrapper<cl_int>(clGetDeviceInfo, deviceId, CL_DEVICE_PCI_BUS_ID_NV);
	cl_int slot_id = clGetWrapper<cl_int>(clGetDeviceInfo, deviceId, CL_DEVICE_PCI_SLOT_ID_NV);
	return (bus_id << 16) + slot_id;
}

template <typename T> bool printResult(const T & t, const cl_int & err) {
	std::cout << t << ": " << ((err != CL_SUCCESS) ? toString(err) : "OK") << std::endl;
	return err != CL_SUCCESS;
}

bool printResult(const cl_int err) {
	std::cout << ((err != CL_SUCCESS) ? toString(err) : "OK") << std::endl;
	return err != CL_SUCCESS;
}

std::string getDeviceCacheFilename(cl_device_id & d, const size_t & inverseSize) {
	const auto uniqueId = getUniqueDeviceIdentifier(d);
	return "cache-opencl." + toString(inverseSize) + "." + toString(uniqueId);
}

int main(int argc, char * * argv) {
	try {
		ArgParser argp(argc, argv);
		bool bHelp = false;
		bool bModeBenchmark = false;
		bool bModeZeros = false;
		bool bModeLetters = false;
		bool bModeNumbers = false;
		std::string strModeLeading;
		std::string strModeMatching;
		bool bModeLeadingRange = false;
		bool bModeRange = false;
		bool bModeMirror = false;
		bool bModeDoubles = false;
		int rangeMin = 0;
		int rangeMax = 0;
		std::string strVanityFile;
		int prefixLen = 0;
		int suffixLen = 0;
		std::vector<size_t> vDeviceSkipIndex;
		size_t worksizeLocal = 64;
		size_t worksizeMax = 0;
		bool bNoCache = false;
		size_t inverseSize = 255;
		size_t inverseMultiple = 16384;
		bool bQuitAfterOne = false;

		argp.addSwitch('h', "help", bHelp);
		argp.addSwitch('0', "benchmark", bModeBenchmark);
		argp.addSwitch('1', "zeros", bModeZeros);
		argp.addSwitch('2', "letters", bModeLetters);
		argp.addSwitch('3', "numbers", bModeNumbers);
		argp.addSwitch('4', "leading", strModeLeading);
		argp.addSwitch('5', "matching", strModeMatching);
		argp.addSwitch('6', "leading-range", bModeLeadingRange);
		argp.addSwitch('7', "range", bModeRange);
		argp.addSwitch('8', "mirror", bModeMirror);
		argp.addSwitch('9', "leading-doubles", bModeDoubles);
		argp.addSwitch('m', "min", rangeMin);
		argp.addSwitch('M', "max", rangeMax);
		argp.addSwitch('f', "file", strVanityFile);
		argp.addSwitch('t', "top", prefixLen);
		argp.addSwitch('w', "bottom", suffixLen);
		argp.addMultiSwitch('s', "skip", vDeviceSkipIndex);
		argp.addSwitch('l', "work", worksizeLocal);
		argp.addSwitch('W', "work-max", worksizeMax);
		argp.addSwitch('n', "no-cache", bNoCache);
		argp.addSwitch('i', "inverse-size", inverseSize);
		argp.addSwitch('I', "inverse-multiple", inverseMultiple);
		argp.addSwitch('q', "quit", bQuitAfterOne);

		if (!argp.parse()) {
			std::cout << "error: bad arguments, try again :<" << std::endl;
			return 1;
		}

		if (bHelp) {
			std::cout << g_strHelp << std::endl;
			return 0;
		}

		Mode mode = Mode::numbers();
		if (bModeBenchmark) mode = Mode::benchmark();
		else if (bModeZeros) mode = Mode::zeros();
		else if (bModeLetters) mode = Mode::letters();
		else if (bModeNumbers) mode = Mode::numbers();
		else if (!strModeLeading.empty()) mode = Mode::leading(strModeLeading[0]);
		else if (!strModeMatching.empty()) mode = Mode::matching(strModeMatching);
		else if (bModeLeadingRange) mode = Mode::leadingRange(rangeMin, rangeMax);
		else if (bModeRange) mode = Mode::range(rangeMin, rangeMax);
		else if (bModeMirror) mode = Mode::mirror();
		else if (bModeDoubles) mode = Mode::doubles();
		else if (!strVanityFile.empty()) mode = Mode::vanityFile(strVanityFile, prefixLen, suffixLen);
		else {}

		auto vDevicesAll = getAllDevices();
		std::vector<cl_device_id> vDevices;
		for (size_t i = 0; i < vDevicesAll.size(); ++i) {
			bool bSkip = false;
			for (auto & skipIndex : vDeviceSkipIndex) {
				if (skipIndex == i) {
					bSkip = true;
					break;
				}
			}

			if (!bSkip) {
				vDevices.push_back(vDevicesAll[i]);
			}
		}

		if (vDevices.empty()) {
			std::cout << "error: no devices found" << std::endl;
			return 1;
		}

		std::cout << "Found " << vDevices.size() << " devices" << std::endl;
		for (auto & d : vDevices) {
			std::cout << "  Device: " << clGetWrapperString(clGetDeviceInfo, d, CL_DEVICE_NAME) << std::endl;
		}
		std::cout << std::endl;

		cl_int res;
		auto clContext = clCreateContext(NULL, vDevices.size(), vDevices.data(), NULL, NULL, &res);
		if (printResult("Creating context", res)) return 1;

		std::string strSource = getOpenCLSource();
		const char * szSource = strSource.c_str();
		size_t szSourceLen = strSource.size();
		auto clProgram = clCreateProgramWithSource(clContext, 1, &szSource, &szSourceLen, &res);
		if (printResult("Creating program", res)) return 1;

		std::cout << "Building program... " << std::flush;
		std::string strBuildOptions = "-D PROFANITY_INVERSE_SIZE=" + toString(inverseSize) + " -D PROFANITY_MAX_SCORE=40";
		res = clBuildProgram(clProgram, vDevices.size(), vDevices.data(), strBuildOptions.c_str(), NULL, NULL);
		if (res != CL_SUCCESS) {
			std::cout << toString(res) << std::endl;
			for (auto & d : vDevices) {
				std::cout << "Build log for " << clGetWrapperString(clGetDeviceInfo, d, CL_DEVICE_NAME) << ":" << std::endl;
				std::cout << clGetWrapperString(clGetProgramBuildInfo, clProgram, d, CL_PROGRAM_BUILD_LOG) << std::endl;
			}
			return 1;
		}
		std::cout << "OK" << std::endl;

		Dispatcher d(clContext, clProgram, mode, worksizeMax == 0 ? inverseSize * inverseMultiple : worksizeMax, inverseSize, inverseMultiple, bQuitAfterOne ? 1 : 0);
		for (size_t i = 0; i < vDevices.size(); ++i) {
			d.addDevice(vDevices[i], worksizeLocal, i);
		}

		clReleaseProgram(clProgram);
		clReleaseContext(clContext);

	} catch (std::exception & e) {
		std::cout << "error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
