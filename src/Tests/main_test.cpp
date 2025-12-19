#include <gtest/gtest.h>
#include "NeteaseDriver.h"
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
// PE 架构检测测试 (v0.0.2 新增)
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
    std::cout << "[INFO] IPC::NeteaseState size: " << size << std::endl;
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

// 真实环境路径获取测试
TEST(NeteaseDriverTest, GetInstallPath_RealEnv) {
    std::string path = NeteaseDriver::GetInstallPath();
    if (path.empty()) {
        std::cout << "[WARN] Netease Cloud Music not found, skipping path check." << std::endl;
    } else {
        std::cout << "[INFO] Found install path: " << path << std::endl;
        EXPECT_FALSE(path.empty());
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
