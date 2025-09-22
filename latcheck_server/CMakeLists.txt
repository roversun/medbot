cmake_minimum_required(VERSION 3.19)
project(latcheck_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt6组件 - 移除HttpServer
find_package(Qt6 6.5 REQUIRED COMPONENTS 
    Core 
    Network 
    Sql 
    Concurrent
)

# 查找OpenSSL
find_package(OpenSSL REQUIRED)

# 查找MySQL客户端库
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient)

qt_standard_project_setup()

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MYSQLCLIENT_INCLUDE_DIRS}
)

# 源文件 - 移除REST API源文件
set(SOURCES
    main.cpp
    src/config/config_manager.cpp
    src/logger/logger.cpp
    src/database/database_pool.cpp
    src/database/base_dao.cpp
    src/database/user_dao.cpp
    src/database/report_dao.cpp
    src/protocol/message_protocol.cpp
    src/server/tls_server.cpp
    src/auth/auth_manager.cpp
    src/auth/password_utils.cpp  # 添加这一行
)

# 头文件 - 移除REST API头文件
set(HEADERS
    include/config/config_manager.h
    include/logger/logger.h
    include/database/database_pool.h
    include/database/base_dao.h
    include/database/user_dao.h
    include/database/report_dao.h
    include/protocol/message_protocol.h
    include/server/tls_server.h
    include/auth/auth_manager.h
    include/common/error_codes.h
    include/common/types.h
)

# 创建可执行文件
qt_add_executable(latcheck_server
    ${SOURCES}
    ${HEADERS}
)

# 链接库 - 移除Qt::HttpServer
target_link_libraries(latcheck_server
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Sql
        Qt::Concurrent
        OpenSSL::SSL
        OpenSSL::Crypto
        ${MYSQLCLIENT_LIBRARIES}
)

# 编译选项
target_compile_options(latcheck_server PRIVATE 
    ${MYSQLCLIENT_CFLAGS_OTHER}
    -Wall -Wextra -O2
)

# 链接目录
target_link_directories(latcheck_server PRIVATE ${MYSQLCLIENT_LIBRARY_DIRS})

# 定义使用MySQL
target_compile_definitions(latcheck_server PRIVATE USE_MYSQL)

# 安装配置
include(GNUInstallDirs)

install(TARGETS latcheck_server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装配置文件
install(FILES config/config.json
    DESTINATION /etc/ipcheck/
)

# 创建日志目录
install(DIRECTORY DESTINATION /var/log/latcheck_server
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                         GROUP_READ GROUP_EXECUTE
                         WORLD_READ WORLD_EXECUTE
)