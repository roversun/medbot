#!/bin/bash

# LatCheck Server 编译脚本 for Ubuntu
# 使用方法: ./build.sh [clean|install|run]

set -e  # 遇到错误时退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查是否为root用户
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_warning "不建议以root用户运行此脚本"
        read -p "是否继续? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# 安装依赖
install_dependencies() {
    print_info "正在安装编译依赖..."
    
    # 更新包列表
    sudo apt update
    
    # 安装基础编译工具
    sudo apt install -y \
        build-essential \
        cmake \
        pkg-config \
        git
    
    # 安装Qt6开发包
    sudo apt install -y \
        qt6-base-dev \
        qt6-base-dev-tools \
        libqt6core6 \
        libqt6network6 \
        libqt6sql6 \
        libqt6sql6-mysql \
        libqt6httpserver6-dev
    
    # 安装MySQL客户端开发包
    sudo apt install -y \
        libmysqlclient-dev \
        mysql-client
    
    # 安装SSL开发包
    sudo apt install -y \
        libssl-dev
    
    print_success "依赖安装完成"
}

# 检查依赖
check_dependencies() {
    print_info "检查编译依赖..."

    return 0;
    
    local missing_deps=()
    
    # 检查基础工具
    command -v cmake >/dev/null 2>&1 || missing_deps+=("cmake")
    command -v g++ >/dev/null 2>&1 || missing_deps+=("g++")
    command -v pkg-config >/dev/null 2>&1 || missing_deps+=("pkg-config")
    
    # 检查Qt6
    if ! pkg-config --exists Qt6Core; then
        missing_deps+=("Qt6Core")
    fi
    
    # 检查MySQL
    if ! pkg-config --exists mysqlclient; then
        missing_deps+=("mysqlclient")
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "缺少以下依赖: ${missing_deps[*]}"
        print_info "运行 './build.sh install' 来安装依赖"
        return 1
    fi
    
    print_success "所有依赖已满足"
    return 0
}

# 创建必要的目录
setup_directories() {
    print_info "创建必要的目录..."
    
    mkdir -p build
    mkdir -p logs
    mkdir -p config/certs
    
    print_success "目录创建完成"
}

# 生成自签名SSL证书（如果不存在）
generate_ssl_certs() {
    local cert_dir="config/certs"
    local cert_file="$cert_dir/server.crt"
    local key_file="$cert_dir/server.key"
    
    if [[ -f "$cert_file" && -f "$key_file" ]]; then
        print_info "SSL证书已存在，跳过生成"
        return 0
    fi
    
    print_info "生成自签名SSL证书..."
    
    # 创建证书配置文件
    cat > "$cert_dir/server.conf" << EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
C = CN
ST = Beijing
L = Beijing
O = LatCheck Server
OU = IT Department
CN = localhost

[v3_req]
keyUsage = keyEncipherment, dataEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
DNS.2 = *.localhost
IP.1 = 127.0.0.1
IP.2 = ::1
EOF
    
    # 生成私钥
    openssl genrsa -out "$key_file" 2048
    
    # 生成证书
    openssl req -new -x509 -key "$key_file" -out "$cert_file" -days 365 \
        -config "$cert_dir/server.conf" -extensions v3_req
    
    # 设置权限
    chmod 600 "$key_file"
    chmod 644 "$cert_file"
    
    print_success "SSL证书生成完成"
}

# 清理构建目录
clean_build() {
    print_info "清理构建目录..."
    
    if [ -d "build" ]; then
        rm -rf build/*
        print_success "构建目录清理完成"
    else
        print_info "构建目录不存在，无需清理"
    fi
}

# 配置CMake
configure_cmake() {
    print_info "配置CMake..."
    
    cd build
    
    # 设置Qt6路径 - 使用用户自定义安装路径
    local qt6_base_dir="/home/kevin/Qt"
    local qt6_version="6.9.1"  # 根据你的实际版本调整
    local qt6_dir="$qt6_base_dir/$qt6_version/gcc_64/lib/cmake/Qt6"
    
    # 检查Qt6路径是否存在
    if [ ! -d "$qt6_dir" ]; then
        # 尝试查找实际的Qt6版本目录
        print_info "正在查找Qt6安装目录..."
        for version_dir in "$qt6_base_dir"/*/gcc_64/lib/cmake/Qt6; do
            if [ -d "$version_dir" ]; then
                qt6_dir="$version_dir"
                print_info "找到Qt6路径: $qt6_dir"
                break
            fi
        done
    fi
    
    if [ -d "$qt6_dir" ]; then
        print_info "使用Qt6路径: $qt6_dir"
        # 设置环境变量
        export CMAKE_PREFIX_PATH="$qt6_dir:$CMAKE_PREFIX_PATH"
        export Qt6_DIR="$qt6_dir"
        
        # CMake配置
        cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=/usr/local \
            -DQt6_DIR="$qt6_dir" \
            -DCMAKE_PREFIX_PATH="$qt6_dir"
    else
        print_error "未找到Qt6安装目录，请检查路径: $qt6_base_dir"
        print_info "请确保Qt6已正确安装到指定目录"
        return 1
    fi
    
    cd ..
    
    print_success "CMake配置完成"
}

# 编译项目
build_project() {
    print_info "开始编译项目..."
    
    cd build
    
    # 获取CPU核心数用于并行编译
    local cores=$(nproc)
    print_info "使用 $cores 个核心进行并行编译"
    
    # 编译
    make -j$cores
    
    cd ..
    
    print_success "项目编译完成"
}

# 运行服务器
run_server() {
    print_info "启动LatCheck服务器..."
    
    if [ ! -f "build/latcheck_server" ]; then
        print_error "可执行文件不存在，请先编译项目"
        return 1
    fi
    
    # 检查配置文件
    if [ ! -f "config/config.json" ]; then
        print_warning "配置文件不存在，请确保config/config.json存在"
        return 1
    fi
    
    # 检查SSL证书
    if [ ! -f "config/certs/server.crt" ] || [ ! -f "config/certs/server.key" ]; then
        print_warning "SSL证书不存在，正在生成..."
        generate_ssl_certs
    fi
    
    print_info "服务器启动中..."
    ./build/latcheck_server
}

# 显示帮助信息
show_help() {
    echo "LatCheck Server 编译脚本"
    echo ""
    echo "使用方法:"
    echo "  $0 [选项]"
    echo ""
    echo "选项:"
    echo "  install    安装编译依赖"
    echo "  clean      清理构建目录"
    echo "  build      编译项目（默认）"
    echo "  run        运行服务器"
    echo "  help       显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 install  # 安装依赖"
    echo "  $0 clean    # 清理并重新编译"
    echo "  $0          # 直接编译"
    echo "  $0 run      # 运行服务器"
}

# 主函数
main() {
    local action="${1:-build}"
    
    case "$action" in
        "install")
            check_root
            install_dependencies
            ;;
        "clean")
            clean_build
            print_success "构建目录清理完成"
            return 0
            setup_directories
            generate_ssl_certs
            if check_dependencies; then
                configure_cmake
                build_project
            fi
            ;;
        "build")
            setup_directories
            generate_ssl_certs
            if check_dependencies; then
                configure_cmake
                build_project
            else
                print_error "请先安装依赖: ./build.sh install"
                exit 1
            fi
            ;;
        "run")
            run_server
            ;;
        "help")
            show_help
            ;;
        *)
            print_error "未知选项: $action"
            show_help
            exit 1
            ;;
    esac
}

# 脚本入口
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi