-- 创建数据库
CREATE DATABASE IF NOT EXISTS latcheck CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE latcheck;

-- 删除现有的表（按照外键依赖关系顺序删除）
DROP TABLE IF EXISTS report_record;
DROP TABLE IF EXISTS latcheck_report;
DROP TABLE IF EXISTS test_server;
DROP TABLE IF EXISTS users;

-- 创建用户表
CREATE TABLE IF NOT EXISTS users (
    user_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    salt VARCHAR(255) NOT NULL,
    email VARCHAR(100),
    role TINYINT NOT NULL DEFAULT 2 COMMENT '0=Admin, 1=ReportUploader, 2=ReportViewer',
    status TINYINT NOT NULL DEFAULT 0 COMMENT '0=Active, 1=Inactive, 2=Suspended, 3=Deleted',
    login_attempts INT DEFAULT 0,
    locked_until DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    last_login_at DATETIME NULL,
    INDEX idx_username (username),
    INDEX idx_status (status),
    INDEX idx_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 创建报告表
CREATE TABLE IF NOT EXISTS latcheck_report (
    report_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_name VARCHAR(50) NOT NULL,
    check_location VARCHAR(255) NOT NULL,
    status TINYINT NOT NULL DEFAULT 0 COMMENT '0=Pending, 1=Processing, 2=Completed, 3=Failed',
    created_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_user_name (user_name),
    INDEX idx_location (check_location),
    INDEX idx_created_time (created_time),
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 创建测试服务器表（移到report_record表之前）
CREATE TABLE IF NOT EXISTS test_server (
    server_id INT AUTO_INCREMENT PRIMARY KEY COMMENT '服务器ID，主键',
    location VARCHAR(128) UNIQUE NOT NULL COMMENT '位置信息，唯一',
    ip_addr INT UNSIGNED NOT NULL COMMENT 'IPv4地址整数值',
    update_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    active BOOLEAN DEFAULT TRUE COMMENT '是否活跃',
    INDEX idx_location (location),
    INDEX idx_active (active)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


CREATE TABLE IF NOT EXISTS report_record (
    record_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    report_id BIGINT NOT NULL,
    server_ip INT UNSIGNED NOT NULL, -- 存储为无符号整数以支持完整的IPv4范围
    server_id INT NOT NULL COMMENT '关联测试服务器ID', 
    latency INT UNSIGNED NOT NULL DEFAULT 10000 CHECK (latency BETWEEN 0 AND 10000),
    INDEX idx_report_id (report_id),
    INDEX idx_server_id (server_id),
    FOREIGN KEY (report_id) REFERENCES latcheck_report(report_id) ON DELETE CASCADE,
    FOREIGN KEY (server_id) REFERENCES test_server(server_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 插入默认管理员用户（密码：Medbot8848）
INSERT INTO users (username, password_hash, salt, role, status) VALUES 
('admin', 
 SHA2(CONCAT('Medbot8848', 'initial_admin_salt_2025'), 256), 
 'initial_admin_salt_2025', 
 0,  -- Admin角色
 0 -- 启用状态（对应UserStatus::Active）
) ON DUPLICATE KEY UPDATE username=username;

-- 创建数据库用户（可选）
-- CREATE USER IF NOT EXISTS 'latcheck_user'@'localhost' IDENTIFIED BY 'your_secure_password';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON latcheck.* TO 'latcheck_user'@'localhost';
-- FLUSH PRIVILEGES;