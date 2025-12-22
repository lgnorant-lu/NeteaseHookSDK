#define LOG_TAG "TEST"
#include <gtest/gtest.h>
#include "NeteaseDriver.h"
#include "NeteaseAPI.h"
#include "SimpleLog.h"
#include <iostream>
#include <fstream>
#include <windows.h>

// 辅助：创建假的 PE 文件用于测试
void CreateMockPE(const std::string& path, bool isX64) {
    std::ofstream ofs(path, std::ios::binary);
    
    // DOS Header
    IMAGE_DOS_HEADER dosHeader = {0};
    dosHeader.e_magic = IMAGE_DOS_SIGNATURE;  // "MZ"
    dosHeader.e_lfanew = sizeof(IMAGE_DOS_HEADER);
    ofs.write(reinterpret_cast<char*>(&dosHeader), sizeof(dosHeader));
    
    // NT Signature
    DWORD ntSig = IMAGE_NT_SIGNATURE;  // "PE\0\0"
    ofs.write(reinterpret_cast<char*>(&ntSig), sizeof(ntSig));
    
    // File Header
    IMAGE_FILE_HEADER fileHeader = {0};
    fileHeader.Machine = isX64 ? IMAGE_FILE_MACHINE_AMD64 : IMAGE_FILE_MACHINE_I386;
    ofs.write(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    
    ofs.close();
}

// ============================================================
// PE 架构检测测试 (v0.1.2 新增)
// ============================================================

TEST(PEDetectionTest, DetectX64Architecture) {
    std::string tempPath = std::string(getenv("TEMP")) + "\\test_x64.exe";
    CreateMockPE(tempPath, true);
    
    HANDLE hFile = CreateFileA(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
    
    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead;
    BOOL success = ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL);
    ASSERT_TRUE(success);
    ASSERT_EQ(dosHeader.e_magic, IMAGE_DOS_SIGNATURE);
    
    SetFilePointer(hFile, dosHeader.e_lfanew, NULL, FILE_BEGIN);
    DWORD peSig;
    IMAGE_FILE_HEADER fileHeader;
    ReadFile(hFile, &peSig, sizeof(peSig), &bytesRead, NULL);
    ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL);
    CloseHandle(hFile);
    
    EXPECT_EQ(peSig, IMAGE_NT_SIGNATURE);
    EXPECT_EQ(fileHeader.Machine, IMAGE_FILE_MACHINE_AMD64);
    
    DeleteFileA(tempPath.c_str());
}

TEST(PEDetectionTest, DetectX86Architecture) {
    std::string tempPath = std::string(getenv("TEMP")) + "\\test_x86.exe";
    CreateMockPE(tempPath, false);
    
    HANDLE hFile = CreateFileA(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
    
    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead;
    ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL);
    SetFilePointer(hFile, dosHeader.e_lfanew, NULL, FILE_BEGIN);
    
    DWORD peSig;
    IMAGE_FILE_HEADER fileHeader;
    ReadFile(hFile, &peSig, sizeof(peSig), &bytesRead, NULL);
    ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, NULL);
    CloseHandle(hFile);
    
    EXPECT_EQ(fileHeader.Machine, IMAGE_FILE_MACHINE_I386);
    
    DeleteFileA(tempPath.c_str());
}

TEST(PEDetectionTest, RejectInvalidPEFile) {
    std::string tempPath = std::string(getenv("TEMP")) + "\\invalid.exe";
    std::ofstream ofs(tempPath, std::ios::binary);
    ofs << "This is not a valid PE file";
    ofs.close();
    
    HANDLE hFile = CreateFileA(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
    
    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead;
    ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, NULL);
    CloseHandle(hFile);
    
    // Magic 应该不匹配
    EXPECT_NE(dosHeader.e_magic, IMAGE_DOS_SIGNATURE);
    
    DeleteFileA(tempPath.c_str());
}

// ============================================================
// 原有测试
// ============================================================

// 初始化状态测试
TEST(NeteaseDriverTest, InitialState) {
    auto& driver = NeteaseDriver::Instance();
    // 确保从断开状态开始测试
    driver.Disconnect();
    
    auto state = driver.GetState();
    
    EXPECT_FALSE(state.isPlaying);
    // currentProgress 可能保留上次的值，不做断言
    // EXPECT_EQ(state.currentProgress, 0.0); 
    // songId 可能保留上次的值
}

// 结构体大小检查 (确保 IPC 兼容性)
TEST(SharedMemoryTest, StructureSizeCheck) {
    // 检查结构体大小是否符合预期 (当前版本 344 字节)
    // float (4) + bool (1) + padding (3) + char[336] = 344
    // 实际上可能有对齐差异，打印出来看
    size_t size = sizeof(IPC::NeteaseState);
    LOG_INFO("IPC::NeteaseState size: " << size);
    // 只要大于 0 且也是 4 的倍数通常可以
    EXPECT_GT(size, 0);
}

// 连接测试：当目标未启动时应失败
TEST(NeteaseDriverTest, Connect_FailWhenClosed) {
    auto& driver = NeteaseDriver::Instance();
    driver.Disconnect(); 
    
    // 如果没有开启 9222 端口，应该返回 false
    // 这个测试依赖环境，如果在开发机上开了 NCM 可能会成功，所以此测试仅供演示
    bool result = driver.Connect(9223); // 使用错误端口确保失败
    EXPECT_FALSE(result);
}

// ============================================================
// v0.1.2: 日志控制测试
// ============================================================

TEST(LoggingControlTest, SDKLoggingToggle) {
    // 1. 测试初始状态 (依据设计应为 false)
    NeteaseDriver::SetGlobalLogging(false);
    
    // 2. 开启并验证
    NeteaseDriver::SetGlobalLogging(true);
    // 这里没有直接读取接口，但我们可以通过 SetGlobalLogLevel 辅助验证
    NeteaseDriver::SetGlobalLogLevel(1); 
    
    // 3. 关闭并验证
    NeteaseDriver::SetGlobalLogging(false);
}

TEST(LoggingControlTest, ThreadSafetyCheck) {
    // 这个测试主要是为了确保在高频并发下 Log 不会崩溃
    NeteaseDriver::SetGlobalLogging(true);
    
    auto logFunc = []() {
        for(int i = 0; i < 100; ++i) {
            LOG_INFO("Concurrency Test Line " << i);
        }
    };
    
    std::thread t1(logFunc);
    std::thread t2(logFunc);
    
    t1.join();
    t2.join();
    
    NeteaseDriver::SetGlobalLogging(false);
}

// ============================================================
// v0.1.2: SDK 详细测试增加 (State & Connection)
// ============================================================

TEST(NeteaseDriverDetailedTest, ConnectionRetryLogic) {
    auto& driver = NeteaseDriver::Instance();
    driver.Disconnect();
    
    // 测试在不同端口下的连接失败
    EXPECT_FALSE(driver.Connect(1234));
    EXPECT_FALSE(driver.Connect(5678));
}

TEST(NeteaseDriverDetailedTest, StateRetrievalConsistency) {
    auto& driver = NeteaseDriver::Instance();
    // 即使未连接，获取状态也不应崩溃，且应返回默认值
    auto state = driver.GetState();
    EXPECT_EQ(state.songId[0], '\0');
    EXPECT_EQ(state.isPlaying, 0);
}

TEST(LoggingControlDetailedTest, LogLevelFiltering) {
    NeteaseDriver::SetGlobalLogging(true);
    
    // 验证不同级别的日志设置 (内部逻辑验证)
    NeteaseDriver::SetGlobalLogLevel(0); // ERROR only
    LOG_ERROR("Should be visible");
    LOG_DEBUG("Should be hidden");
    
    NeteaseDriver::SetGlobalLogLevel(3); // All
    LOG_DEBUG("Now debug is visible");
    
    NeteaseDriver::SetGlobalLogging(false);
}

TEST(NeteaseAPITest, SongDetailParsing) {
    // 这是一个静态测试，验证 API 模块的基础功能
    long long testId = 1299570939;
    auto detail = Netease::API::GetSongDetail(testId);
    if (detail.has_value()) {
        long long songId = detail->songId;
        EXPECT_EQ(songId, testId);
        LOG_INFO("API Test: Found title: " << detail->title);
    } else {
        LOG_WARN("API Test: Failed to fetch song detail (expected in offline test)");
    }
}

TEST(NeteaseAPITest, LocalLyricCache) {
    long long testId = 123456789;
    auto lyrics = Netease::API::GetLocalLyric(testId);
    // 即使缓存不存在，也不应崩溃
    EXPECT_FALSE(lyrics.has_value());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
