/**
 * MiniWeb V1.5 — Config-Driven Server
 * W3D5 Config-Driven Server Demo
 */

(function() {
    'use strict';

    // 页面加载完成后执行
    document.addEventListener('DOMContentLoaded', function() {
        console.log('MiniWeb V1.5 loaded successfully!');
        console.log('Static file server is serving:');
        console.log('  - HTML from www/index.html');
        console.log('  - CSS from www/css/style.css');
        console.log('  - JS from www/js/app.js');

        // 在页脚添加加载时间
        var footer = document.querySelector('footer p');
        if (footer) {
            var now = new Date();
            footer.innerHTML += ' | Page loaded at ' + now.toLocaleTimeString();
        }
    });
})();
