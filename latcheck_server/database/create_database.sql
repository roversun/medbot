-- 创建数据库
CREATE DATABASE IF NOT EXISTS latcheck CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE latcheck;

-- 创建用户表
CREATE TABLE IF NOT EXISTS users (
    user_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    salt VARCHAR(255) NOT NULL,
    email VARCHAR(100),
    role TINYINT NOT NULL DEFAULT 0 COMMENT '0=User, 1=Admin, 2=SuperAdmin',
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

-- 创建报告详情表
CREATE TABLE IF NOT EXISTS report_details (
    detail_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    report_id BIGINT NOT NULL,
    server_name VARCHAR(100) NOT NULL,
    server_ip VARCHAR(45) NOT NULL,
    latency DECIMAL(10,3) NOT NULL DEFAULT 0.000,
    status VARCHAR(20) NOT NULL,
    test_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    additional_info TEXT,
    INDEX idx_report_id (report_id),
    INDEX idx_server_name (server_name),
    INDEX idx_test_time (test_time),
    FOREIGN KEY (report_id) REFERENCES latcheck_report(report_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 插入默认管理员用户（密码：admin123，请在生产环境中修改）
INSERT INTO users (username, password_hash, salt, role, status) VALUES 
('admin', 
 SHA2(CONCAT('admin123', 'default_salt'), 256), 
 'default_salt', 
 2, 
 0
) ON DUPLICATE KEY UPDATE username=username;

-- 创建数据库用户（可选）
-- CREATE USER IF NOT EXISTS 'latcheck_user'@'localhost' IDENTIFIED BY 'your_secure_password';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON latcheck.* TO 'latcheck_user'@'localhost';
-- FLUSH PRIVILEGES;