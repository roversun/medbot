#!/bin/bash

# 数据库导入脚本 - Ubuntu
# 使用方法: ./import_database.sh [mysql_root_password]

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查是否为root用户
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_warn "建议不要使用root用户运行此脚本"
        read -p "是否继续? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# 检查MySQL是否安装
check_mysql() {
    if ! command -v mysql &> /dev/null; then
        log_error "MySQL未安装，正在安装..."
        sudo apt update
        sudo apt install -y mysql-server mysql-client
        
        # 启动MySQL服务
        sudo systemctl start mysql
        sudo systemctl enable mysql
        
        log_info "MySQL安装完成"
    else
        log_info "MySQL已安装"
    fi
}

# 检查MySQL服务状态
check_mysql_service() {
    if ! sudo systemctl is-active --quiet mysql; then
        log_info "启动MySQL服务..."
        sudo systemctl start mysql
    fi
    
    if sudo systemctl is-active --quiet mysql; then
        log_info "MySQL服务运行正常"
    else
        log_error "MySQL服务启动失败"
        exit 1
    fi
}

# 获取MySQL root密码
get_mysql_password() {
    if [ -n "$1" ]; then
        MYSQL_ROOT_PASSWORD="$1"
    else
        echo -n "请输入MySQL root密码: "
        read -s MYSQL_ROOT_PASSWORD
        echo
    fi
}

# 测试MySQL连接
test_mysql_connection() {
    if mysql -u root -p"$MYSQL_ROOT_PASSWORD" -e "SELECT 1;" &> /dev/null; then
        log_info "MySQL连接测试成功"
    else
        log_error "MySQL连接失败，请检查密码"
        exit 1
    fi
}

# 导入数据库
import_database() {
    local sql_file="create_database.sql"
    
    if [ ! -f "$sql_file" ]; then
        log_error "SQL文件 $sql_file 不存在"
        exit 1
    fi
    
    log_info "开始导入数据库..."
    
    if mysql -u root -p"$MYSQL_ROOT_PASSWORD" < "$sql_file"; then
        log_info "数据库导入成功"
    else
        log_error "数据库导入失败"
        exit 1
    fi
}

# 验证数据库
verify_database() {
    log_info "验证数据库结构..."
    
    # 检查数据库是否存在
    if mysql -u root -p"$MYSQL_ROOT_PASSWORD" -e "USE latcheck; SHOW TABLES;" &> /dev/null; then
        log_info "数据库 'latcheck' 创建成功"
        
        # 显示表结构
        echo "数据库表列表:"
        mysql -u root -p"$MYSQL_ROOT_PASSWORD" -e "USE latcheck; SHOW TABLES;"
        
        # 检查默认管理员用户
        echo "\n默认管理员用户:"
        mysql -u root -p"$MYSQL_ROOT_PASSWORD" -e "USE latcheck; SELECT username, role, status FROM users WHERE role = 2;"
        
    else
        log_error "数据库验证失败"
        exit 1
    fi
}

# 设置数据库用户权限（可选）
setup_database_user() {
    read -p "是否创建专用数据库用户? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo -n "请输入数据库用户名 [latcheck_user]: "
        read DB_USER
        DB_USER=${DB_USER:-latcheck_user}
        
        echo -n "请输入数据库用户密码: "
        read -s DB_PASSWORD
        echo
        
        mysql -u root -p"$MYSQL_ROOT_PASSWORD" -e "
            CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASSWORD';
            GRANT SELECT, INSERT, UPDATE, DELETE ON latcheck.* TO '$DB_USER'@'localhost';
            FLUSH PRIVILEGES;
        "
        
        log_info "数据库用户 '$DB_USER' 创建成功"
        log_warn "请在应用配置中使用以下数据库连接信息:"
        echo "  用户名: $DB_USER"
        echo "  密码: [已设置]"
        echo "  数据库: latcheck"
    fi
}

# 主函数
main() {
    log_info "开始执行数据库导入脚本..."
    
    check_root
    check_mysql
    check_mysql_service
    get_mysql_password "$1"
    test_mysql_connection
    import_database
    verify_database
    setup_database_user
    
    log_info "数据库导入完成！"
    log_warn "重要提示:"
    echo "1. 默认管理员用户名: admin"
    echo "2. 默认管理员密码: admin123"
    echo "3. 请在生产环境中立即修改默认密码"
    echo "4. 建议定期备份数据库"
}

# 执行主函数
main "$@"