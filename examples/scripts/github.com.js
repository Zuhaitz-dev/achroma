// github.com.js — quality-of-life improvements on GitHub
// Place at: ~/.config/achroma/scripts/github.com.js
// Runs automatically on every github.com page load.

(function () {
    // Expand all "Load diff" buttons (large or binary files collapse by default)
    document.querySelectorAll('button').forEach(function (btn) {
        if (/load diff/i.test(btn.textContent.trim())) btn.click();
    });

    // Remove the floating feedback / survey nag that covers content
    var nags = [
        '[aria-label="Give feedback"]',
        '.js-survey-container',
        '#hovercard-aria-description',
    ];
    nags.forEach(function (sel) {
        document.querySelectorAll(sel).forEach(function (el) { el.remove(); });
    });

    // On PR pages: click "Load more files" to show all changed files
    var loadMore = document.querySelector('.ajax-pagination-btn');
    if (loadMore) loadMore.click();

    // Restore middle-click on tab links (GitHub intercepts it on some elements)
    document.querySelectorAll('.js-selected-navigation-item').forEach(function (a) {
        a.addEventListener('auxclick', function (e) {
            if (e.button === 1) window.open(a.href, '_blank');
        });
    });
})();
